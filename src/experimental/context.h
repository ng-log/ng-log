// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Implementation details shared by the translation units of the experimental
// logging functions.

#ifndef NGLOG_SRC_EXPERIMENTAL_CONTEXT_H
#define NGLOG_SRC_EXPERIMENTAL_CONTEXT_H

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <format>
#include <ios>
#include <iterator>
#include <limits>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ng-log/experimental/attributes.h"
#include "ng-log/experimental/sink.h"
#include "ng-log/log_severity.h"
#include "ng-log/logging.h"

namespace nglog::experimental::internal {

// Where a message goes in addition to or instead of the logs.
enum class Destination {
  kLog,
  kSyslog,
  kSinkAndLog,
  kStringAndLog,
  kVectorOrLog,
};

// Returns the state of the call site at location.
Site& FindSite(const std::source_location& location);

// Fields the attributes contribute for structured sinks.
class FieldStore final : public FieldWriter {
 public:
  void Add(std::string_view name, std::string_view value) override {
    fields_.push_back(Stored{std::string{name}, std::string{value}});
  }

  // Views of the fields, which are valid until the next field is added.
  std::vector<Field> View() const {
    std::vector<Field> fields(fields_.size());
    std::ranges::transform(fields_, fields.begin(), [](const Stored& field) {
      return Field{field.name, field.value};
    });
    return fields;
  }

 private:
  struct Stored {
    std::string name;
    std::string value;
  };

  std::vector<Stored> fields_;
};

class ContextImpl final : public Context {
 public:
  ContextImpl(const std::source_location& location, bool debug,
              int preserved_errno = 0) noexcept
      : location_{&location},
        debug_{debug},
        preserved_errno_{preserved_errno} {}

  LogSeverity severity() const noexcept override { return severity_; }
  void set_severity(LogSeverity severity) noexcept override {
    severity_ = severity;
  }

  const char* tag() const noexcept override { return tag_; }
  void set_tag(const char* tag) noexcept override { tag_ = tag; }

  bool debug() const noexcept override { return debug_; }

  void set_preserved_errno(int value) noexcept override {
    preserved_errno_ = value;
  }
  int preserved_errno() const noexcept { return preserved_errno_; }

  const std::source_location& location() const noexcept override {
    return *location_;
  }
  // Refers to another copy of the location, which must outlive the context.
  void set_location(const std::source_location& location) noexcept {
    location_ = &location;
  }

  Site& site() override {
    if (site_ == nullptr) {
      site_ = &FindSite(*location_);
    }
    return *site_;
  }

  // Records how often the call site occurred in the counter type of the rate
  // limit, which the occurrence placeholder formats.
  template <class T>
  void set_occurrences(T occurrences) noexcept {
    static_assert(sizeof(T) <= sizeof(occurrences_),
                  "the occurrence count must fit the storage of the largest "
                  "counter type");
    std::memcpy(occurrences_, &occurrences, sizeof(T));
    // Converts like std::format() formats an integer without a
    // specification.
    write_occurrences_ = [](const unsigned char* value, char* text) {
      T recorded;
      std::memcpy(&recorded, value, sizeof(T));
      return std::to_chars(text, text + kOccurrenceTextLength, recorded).ptr;
    };
  }

  // Formats the occurrences into a context of narrow or wide characters.
  template <class Context>
  typename Context::iterator FormatOccurrences(Context& context) const {
    if (write_occurrences_ == nullptr) {
      return context.out();
    }
    std::array<char, kOccurrenceTextLength> text;
    char* const end = write_occurrences_(occurrences_, text.data());
    return std::copy(text.data(), end, context.out());
  }

  // Whether a fatal message terminates only if DFATAL messages are fatal.
  bool debug_fatal = false;
  // Raises the severity to fatal in debug mode once all attributes are
  // configured.
  bool escalate_in_debug = false;
  Destination destination = Destination::kLog;
  // Passes the message to the logging thread instead of sending it.
  bool async = false;
  // Sends a message with sinks only to them.
  bool skip_log_file = false;
  // Add the targets of the destination. A message can have several of them.
  // Null targets are ignored, which the macros treat as absent.
  void AddSink(LogSink* sink) { Add(&Targets::sinks, sink); }
  void AddStructuredSink(Sink* sink) { Add(&Targets::structured_sinks, sink); }
  void AddString(std::string* text) { Add(&Targets::strings, text); }
  void AddVector(std::vector<std::string>* texts) {
    Add(&Targets::vectors, texts);
  }

  std::span<LogSink* const> sinks() const noexcept {
    return View(&Targets::sinks);
  }
  std::span<Sink* const> structured_sinks() const noexcept {
    return View(&Targets::structured_sinks);
  }
  std::span<std::string* const> strings() const noexcept {
    return View(&Targets::strings);
  }
  std::span<std::vector<std::string>* const> vectors() const noexcept {
    return View(&Targets::vectors);
  }

  // Fields of the attributes, which are collected for structured sinks only.
  FieldStore& fields() { return targets().fields; }
  std::vector<Field> FieldViews() const {
    return targets_ ? targets_->fields.View() : std::vector<Field>{};
  }

 private:
  // Large enough for every counter type.
  unsigned char occurrences_[sizeof(unsigned long long)] = {};
  // Length of the decimal text of any counter value. The largest unsigned
  // value has one digit more than digits10, as many characters as a signed
  // value with its sign.
  static constexpr std::size_t kOccurrenceTextLength =
      std::numeric_limits<unsigned long long>::digits10 + 1;
  // Writes the decimal text of the occurrences and returns its end.
  char* (*write_occurrences_)(const unsigned char* value, char* text) = nullptr;
  const std::source_location* location_;
  bool debug_;
  int preserved_errno_;
  LogSeverity severity_ = NGLOG_INFO;
  const char* tag_ = nullptr;
  Site* site_ = nullptr;

  // Targets of the destination and fields for structured sinks. They are
  // created on first use, since even empty containers allocate with the
  // checked iterators of MSVC, and signal-safe messages must not allocate.
  struct Targets {
    std::vector<LogSink*> sinks;
    std::vector<Sink*> structured_sinks;
    FieldStore fields;
    std::vector<std::string*> strings;
    std::vector<std::vector<std::string>*> vectors;
  };

  std::optional<Targets> targets_;

  Targets& targets() { return targets_ ? *targets_ : targets_.emplace(); }

  template <class T>
  void Add(std::vector<T*> Targets::* list, T* target) {
    if (target != nullptr) {
      (targets().*list).push_back(target);
    }
  }

  template <class T>
  std::span<T* const> View(std::vector<T*> Targets::* list) const noexcept {
    if (!targets_) {
      return {};
    }
    return (*targets_).*list;
  }
};

// Restores errno to the value the message reports, the caller's unless an
// attribute replaced it, when the dispatch returns. This also covers messages
// that are not logged, so that hooks touching errno do not affect the caller.
class ErrnoRestorer {
 public:
  explicit ErrnoRestorer(const ContextImpl& context) noexcept
      : context_{&context} {}
  ErrnoRestorer(const ErrnoRestorer& other) = delete;
  ErrnoRestorer& operator=(const ErrnoRestorer& other) = delete;
  ~ErrnoRestorer() { errno = context_->preserved_errno(); }

 private:
  const ContextImpl* context_;
};

inline ContextImpl& Impl(Context& context) noexcept {
  return static_cast<ContextImpl&>(context);
}

class MessageImpl final : public Message {
 public:
  explicit MessageImpl(LogMessage& message) noexcept : message_{&message} {}

  void Append(std::string_view text) override {
    message_->stream().write(text.data(),
                             static_cast<std::streamsize>(text.size()));
  }

  int preserved_errno() const noexcept override {
    return message_->preserved_errno();
  }

  std::ostreambuf_iterator<char> out() override {
    return std::ostreambuf_iterator<char>{message_->stream().rdbuf()};
  }

  LogMessage& message() const noexcept { return *message_; }

 private:
  LogMessage* message_;
};

// Runs a hook of every attribute that defines it, in composition order.
template <class Hook, class Target>
void RunHooks(std::span<const ErasedAttribute> attributes, Hook Hooks::* hook,
              Target& target) {
  for (const ErasedAttribute& attribute : attributes) {
    if (const Hook run = attribute.hooks->*hook; run != nullptr) {
      run(attribute.attribute, target);
    }
  }
}

// Runs a hook that decides whether to log the message until one of the
// attributes rejects it.
template <class Hook>
bool PassHooks(std::span<const ErasedAttribute> attributes, Hook Hooks::* hook,
               Context& context) {
  return std::ranges::all_of(
      attributes, [hook, &context](const ErasedAttribute& attribute) {
        const Hook run = attribute.hooks->*hook;
        return run == nullptr || run(attribute.attribute, context);
      });
}

}  // namespace nglog::experimental::internal

#endif  // NGLOG_SRC_EXPERIMENTAL_CONTEXT_H
