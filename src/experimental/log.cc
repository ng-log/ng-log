// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Dispatches messages of the experimental logging functions through the
// message implementation of the logging macros.

#include "ng-log/experimental/log.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iterator>
#include <memory>
#include <new>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "async.h"
#include "context.h"
#include "internal/log_message_access.h"
#include "ng-log/experimental/attributes.h"
#include "ng-log/experimental/sink.h"
#include "ng-log/flags.h"
#include "ng-log/logging.h"

namespace nglog::experimental::internal {
namespace {

// Context of the message formatted on this thread, which the occurrence
// placeholder reads.
thread_local const ContextImpl* formatting_context = nullptr;

// Exposes the context of a message while formatting it. Restores the previous
// one, so that formatters may log themselves.
class FormattingScope {
 public:
  explicit FormattingScope(const ContextImpl& context) noexcept
      : previous_{std::exchange(formatting_context, &context)} {}
  FormattingScope(const FormattingScope& other) = delete;
  FormattingScope& operator=(const FormattingScope& other) = delete;
  ~FormattingScope() { formatting_context = previous_; }

 private:
  const ContextImpl* previous_;
};

// Counter value of messages the macros log without a counter.
constexpr std::int64_t kNoCounter = 0;

// Forwards a message to each of several sinks.
class FanOutSink final : public LogSink {
 public:
  explicit FanOutSink(std::span<LogSink* const> sinks) noexcept
      : sinks_{sinks} {}

  void send(LogSeverity severity, const char* full_filename,
            const char* base_filename, int line, const LogMessageTime& time,
            const char* message, std::size_t message_len) override {
    for (LogSink* const sink : sinks_) {
      sink->send(severity, full_filename, base_filename, line, time, message,
                 message_len);
    }
  }

  void WaitTillSent() override {
    for (LogSink* const sink : sinks_) {
      sink->WaitTillSent();
    }
  }

 private:
  std::span<LogSink* const> sinks_;
};

// Sends a message to the structured sinks of a context together with the
// fields of its attributes.
class StructuredSinks final : public LogSink {
 public:
  explicit StructuredSinks(const ContextImpl& context) noexcept
      : context_{&context} {}

  void send(LogSeverity severity, const char* /*full_filename*/,
            const char* /*base_filename*/, int /*line*/,
            const LogMessageTime& time, const char* message,
            std::size_t message_len) override {
    const std::vector<Field> fields = context_->FieldViews();
    const Record record{severity,
                        context_->location(),
                        time.when(),
                        time.gmtoffset(),
                        nglog::GetFlag(FLAGS_log_utc_time),
                        context_->tag(),
                        std::string_view{message, message_len},
                        fields};
    for (Sink* const sink : context_->structured_sinks()) {
      sink->Send(record);
    }
  }

 private:
  const ContextImpl* context_;
};

// The underlying message of the destination a context selects. The message
// must be released using Send() since destroying it can throw.
class Output {
 public:
  explicit Output(const ContextImpl& context)
      : structured_sinks_{context},
        sinks_{Sinks(context, structured_sinks_)},
        fan_out_{sinks_},
        strings_{context.strings()},
        vectors_{context.vectors()} {
    const std::source_location& location = context.location();
    const char* const file = location.file_name();
    const int line = static_cast<int>(location.line());
    const LogSeverity severity = context.severity();

    switch (context.destination) {
      case Destination::kLog:
        if (severity == NGLOG_FATAL && !context.debug_fatal) {
          message_ = new (storage_) LogMessageFatal{file, line};
          fatal_ = true;
        } else {
          message_ = new (storage_) LogMessage{file, line, severity};
        }
        break;
      case Destination::kSyslog:
        // Like SYSLOG(), which has no occurrence counter.
        message_ = new (storage_) LogMessage{file, line, severity, kNoCounter,
                                             &LogMessage::SendToSyslogAndLog};
        break;
      case Destination::kSinkAndLog:
        message_ = new (storage_)
            LogMessage{file, line, severity, Sink(), !context.skip_log_file};
        break;
      case Destination::kStringAndLog:
        message_ =
            new (storage_) LogMessage{file, line, severity, First(strings_)};
        break;
      case Destination::kVectorOrLog:
        message_ =
            new (storage_) LogMessage{file, line, severity, First(vectors_)};
        if (!vectors_.empty()) {
          vector_size_ = vectors_.front()->size();
        }
        break;
    }

    nglog::internal::LogMessageAccess::SetTag(*message_, context.tag());
  }

  Output(const Output& other) = delete;
  Output& operator=(const Output& other) = delete;

  // Flushes the message and releases it. A fatal message terminates. If the
  // failure function throws instead, the exception propagates from here.
  void Send() {
    LogMessage* const message = std::exchange(message_, nullptr);
    if (fatal_) {
      static_cast<LogMessageFatal*>(message)->~LogMessageFatal();
    } else {
      message->~LogMessage();
    }
    // The message stores its text in the first string or vector only.
    if (!strings_.empty()) {
      for (std::string* const text : strings_.subspan(1)) {
        *text = *strings_.front();
      }
    }
    if (!vectors_.empty() && vectors_.front()->size() != vector_size_) {
      for (std::vector<std::string>* const texts : vectors_.subspan(1)) {
        texts->push_back(vectors_.front()->back());
      }
    }
  }

  LogMessage& get() noexcept { return *message_; }

 private:
  // The sink of the message, which forwards to each sink if there are several.
  LogSink* Sink() noexcept {
    if (sinks_.empty()) {
      return nullptr;
    }
    if (sinks_.size() == 1) {
      return sinks_.front();
    }
    return &fan_out_;
  }

  // The sinks of a context, which include an adapter for structured sinks.
  static std::vector<LogSink*> Sinks(const ContextImpl& context,
                                     LogSink& structured_sinks) {
    const std::span<LogSink* const> sinks = context.sinks();
    std::vector<LogSink*> all{sinks.begin(), sinks.end()};
    if (!context.structured_sinks().empty()) {
      all.push_back(&structured_sinks);
    }
    return all;
  }

  template <class T>
  static T* First(std::span<T* const> targets) noexcept {
    return targets.empty() ? nullptr : targets.front();
  }

  StructuredSinks structured_sinks_;
  std::vector<LogSink*> sinks_;
  FanOutSink fan_out_;
  std::span<std::string* const> strings_;
  std::span<std::vector<std::string>* const> vectors_;
  // Size of the first vector before the message appends its text.
  std::size_t vector_size_ = 0;
  alignas(LogMessageFatal) unsigned char storage_[sizeof(LogMessageFatal)];
  LogMessage* message_ = nullptr;
  bool fatal_ = false;
};

static_assert(sizeof(LogMessageFatal) >= sizeof(LogMessage),
              "the storage for a LogMessage must also fit a LogMessageFatal");
static_assert(
    alignof(LogMessageFatal) >= alignof(LogMessage),
    "the storage for a LogMessage must also be aligned for a LogMessageFatal");

}  // namespace

bool IsAdmitted(std::span<const ErasedAttribute> attributes,
                const CallerConfiguration& configuration,
                const std::source_location& location) {
  // Like VLOG_IS_ON(), querying leaves errno untouched.
  const int preserved_errno = errno;
  ContextImpl context{location, configuration.debug};
  RunHooks(attributes, &Hooks::configure, context);
  const bool admitted = PassHooks(attributes, &Hooks::admit, context);
  errno = preserved_errno;
  return admitted;
}

namespace {

// Runs the hooks that decide whether to log a message. Rejected messages and
// stripped ones that are not fatal return false.
bool Admit(std::span<const ErasedAttribute> attributes,
           const CallerConfiguration& configuration, ContextImpl& context) {
  RunHooks(attributes, &Hooks::configure, context);
  // Escalating after all severities are configured makes the order of the
  // attributes irrelevant. Like LOG(DFATAL), the message terminates only if
  // DFATAL messages are fatal. Messages that are fatal already, such as failed
  // checks, keep terminating.
  if (context.escalate_in_debug && context.debug() &&
      context.severity() != NGLOG_FATAL) {
    context.set_severity(NGLOG_FATAL);
    context.debug_fatal = true;
  }

  if (!PassHooks(attributes, &Hooks::admit, context)) {
    return false;
  }

  // Stripped messages are compiled away by the macros, which still terminate
  // on fatal ones. Like the conditions of DLOG() and LOG_IF(), admission is
  // decided first.
  if (context.severity() < configuration.strip_level) {
    if (context.severity() == NGLOG_FATAL) {
      std::abort();
    }
    return false;
  }

  // Rate limits count occurrences regardless of the minimum log level.
  if (!PassHooks(attributes, &Hooks::throttle, context)) {
    return false;
  }

  return nglog::internal::IsLogSeverityEnabled(context.severity());
}

// Composes an admitted message, whose text compose() writes to the message,
// and sends it. The origin, if any, is the call on another thread the message
// reports.
template <class Composer>
void Compose(std::span<const ErasedAttribute> attributes, ContextImpl& context,
             const nglog::internal::LogMessageOrigin* origin,
             const Composer& compose) {
  // Fields are collected only for structured sinks.
  if (!context.structured_sinks().empty()) {
    RunHooks(attributes, &Hooks::fields, context.fields());
  }

  errno = context.preserved_errno();
  Output output = [&context, origin] {
    const nglog::internal::LogMessageOriginScope scope{origin};
    return Output{context};
  }();
  MessageImpl message{output.get()};

  try {
    RunHooks(attributes, &Hooks::prefix, message);

    {
      const FormattingScope scope{context};
      compose(output.get());
    }

    RunHooks(attributes, &Hooks::suffix, message);
  } catch (...) {
    // Logs what has been composed so far like a macro whose stream insertion
    // throws.
    output.Send();
    throw;
  }

  output.Send();
}

// Writes the text of a deferred message. The message refers to the text of a
// wide format string until it is sent.
void FormatDeferred(const DeferredMessage& deferred, LogMessage& message,
                    std::wstring& text) {
  deferred.Format(std::ostreambuf_iterator<char>{message.stream()}, text);
  if (!text.empty()) {
    nglog::internal::LogMessageAccess::AppendWide(message, text.data(),
                                                  text.size());
  }
}

}  // namespace

void Dispatch(std::span<const ErasedAttribute> attributes,
              const CallerConfiguration& configuration,
              const std::source_location& location, std::string_view format,
              std::format_args args) {
  // Hooks and site lookups may clobber errno before the message preserves it.
  // Attributes that captured errno at the call site replace the value.
  ContextImpl context{location, configuration.debug, errno};
  const ErrnoRestorer restore_errno{context};
  if (!Admit(attributes, configuration, context)) {
    return;
  }
  Compose(attributes, context, nullptr, [format, &args](LogMessage& message) {
    std::vformat_to(std::ostreambuf_iterator<char>{message.stream()}, format,
                    args);
  });
}

void Dispatch(std::span<const ErasedAttribute> attributes,
              const CallerConfiguration& configuration,
              const std::source_location& location, std::wstring_view format,
              std::wformat_args args) {
  ContextImpl context{location, configuration.debug, errno};
  const ErrnoRestorer restore_errno{context};
  if (!Admit(attributes, configuration, context)) {
    return;
  }
  // The message refers to the text until it is sent.
  std::wstring text;
  Compose(attributes, context, nullptr,
          [format, &args, &text](LogMessage& message) {
            text = std::vformat(format, args);
            nglog::internal::LogMessageAccess::AppendWide(message, text.data(),
                                                          text.size());
          });
}

void DispatchDeferred(std::span<const ErasedAttribute> attributes,
                      const CallerConfiguration& configuration,
                      const std::source_location& location,
                      const MessageCopier& copier) {
  ContextImpl context{location, configuration.debug, errno};
  const ErrnoRestorer restore_errno{context};
  if (!Admit(attributes, configuration, context)) {
    return;
  }

  std::unique_ptr<DeferredMessage> message = copier.copy(copier.message);
  // A fatal message terminates the calling thread once the logging thread sent
  // the messages passed to it before.
  if (context.severity() == NGLOG_FATAL) {
    WaitForPendingMessages();
    const DeferredMessage& fatal = *message;
    std::wstring text;
    Compose(fatal.attributes(), context, nullptr,
            [&fatal, &text](LogMessage& composed) {
              FormatDeferred(fatal, composed, text);
            });
    return;
  }

  Defer(std::make_unique<PendingMessage>(
      context, std::move(message),
      nglog::internal::CaptureLogMessageOrigin(
          location.file_name(), static_cast<int>(location.line()))));
}

void Send(PendingMessage& message) {
  const DeferredMessage& deferred = message.message();
  std::wstring text;
  Compose(deferred.attributes(), message.context(), &message.origin(),
          [&deferred, &text](LogMessage& composed) {
            FormatDeferred(deferred, composed, text);
          });
}

std::format_context::iterator FormatOccurrence(std::format_context& context) {
  return formatting_context == nullptr
             ? context.out()
             : formatting_context->FormatOccurrences(context);
}

std::wformat_context::iterator FormatOccurrence(std::wformat_context& context) {
  return formatting_context == nullptr
             ? context.out()
             : formatting_context->FormatOccurrences(context);
}

}  // namespace nglog::experimental::internal
