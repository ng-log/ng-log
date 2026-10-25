// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Log() logs a message described by attributes to the log files, the sinks and
// the other regular destinations, and IsEnabled() determines whether attributes
// admit a message.

#ifndef NGLOG_EXPERIMENTAL_LOG_H
#define NGLOG_EXPERIMENTAL_LOG_H

#include "ng-log/experimental/requirements.h"  // IWYU pragma: keep

#if defined(NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_SUPPORTED)

#  include <concepts>
#  include <cstddef>
#  include <format>
#  include <iterator>
#  include <memory>
#  include <source_location>
#  include <span>
#  include <string>
#  include <string_view>
#  include <tuple>
#  include <type_traits>
#  include <utility>

#  include "ng-log/experimental/attributes.h"
#  include "ng-log/experimental/format.h"
#  include "ng-log/experimental/log_call.h"  // IWYU pragma: export
#  include "ng-log/export.h"
#  include "ng-log/log_message.h"

namespace nglog::experimental {

namespace internal {

NGLOG_EXPORT bool IsAdmitted(std::span<const ErasedAttribute> attributes,
                             const CallerConfiguration& configuration,
                             const std::source_location& location);

NGLOG_EXPORT void Dispatch(std::span<const ErasedAttribute> attributes,
                           const CallerConfiguration& configuration,
                           const std::source_location& location,
                           std::string_view format, std::format_args args);

NGLOG_EXPORT void Dispatch(std::span<const ErasedAttribute> attributes,
                           const CallerConfiguration& configuration,
                           const std::source_location& location,
                           std::wstring_view format, std::wformat_args args);

// Copies of the attributes and the arguments of a message, which the logging
// thread composes.
class DeferredMessage {
 public:
  DeferredMessage(const DeferredMessage& other) = delete;
  DeferredMessage& operator=(const DeferredMessage& other) = delete;
  virtual ~DeferredMessage() = default;

  // The copies of the attributes with their types erased.
  virtual std::span<const ErasedAttribute> attributes() const noexcept = 0;
  // Formats the copies of the arguments into out, or into wide for a wide
  // format string, whose text the caller writes.
  virtual void Format(std::ostreambuf_iterator<char> out,
                      std::wstring& wide) const = 0;
  // Bytes of the copies, which count against the capacity of the queue.
  virtual std::size_t size() const noexcept = 0;

 protected:
  DeferredMessage() = default;
};

// Copies a message once its attributes admit it, so that rejected messages
// copy nothing.
struct MessageCopier {
  const void* message;
  std::unique_ptr<DeferredMessage> (*copy)(const void* message);
};

// Decides whether to log a message on the calling thread and passes a copy of
// it to the logging thread, which composes and writes it.
NGLOG_EXPORT void DispatchDeferred(std::span<const ErasedAttribute> attributes,
                                   const CallerConfiguration& configuration,
                                   const std::source_location& location,
                                   const MessageCopier& copier);

template <class T, class CharT>
inline constexpr bool kBorrowedText =
    std::is_same_v<std::decay_t<T>, const CharT*> ||
    std::is_same_v<std::decay_t<T>, CharT*> ||
    std::is_same_v<std::remove_cvref_t<T>, std::basic_string_view<CharT>>;

// The copy of an argument for the logging thread. C strings and string views
// are copied as strings, since their text may change or end with the call.
template <class T>
using DeferredArgument =
    std::conditional_t<kBorrowedText<T, char>, std::string,
                       std::conditional_t<kBorrowedText<T, wchar_t>,
                                          std::wstring, std::decay_t<T>>>;

template <class T>
inline constexpr bool kLazy = false;

template <class F>
inline constexpr bool kLazy<Lazy<F>> = true;

template <class CharT, class A, class... Args>
class DeferredMessageOf final : public DeferredMessage {
 public:
  DeferredMessageOf(const A& attributes, std::basic_string_view<CharT> format,
                    const Args&... arguments)
      : attributes_{attributes},
        erased_{attributes_.Erase()},
        format_{format},
        arguments_{DeferredArgument<Args>(arguments)...} {}

  std::span<const ErasedAttribute> attributes() const noexcept override {
    return erased_;
  }

  void Format([[maybe_unused]] std::ostreambuf_iterator<char> out,
              [[maybe_unused]] std::wstring& wide) const override {
    if constexpr (std::is_same_v<CharT, wchar_t>) {
      wide = std::apply(
          [this](const DeferredArgument<Args>&... arguments) {
            return std::vformat(format_, std::make_wformat_args(arguments...));
          },
          arguments_);
    } else {
      std::apply(
          [this, &out](const DeferredArgument<Args>&... arguments) {
            std::vformat_to(out, format_, std::make_format_args(arguments...));
          },
          arguments_);
    }
  }

  std::size_t size() const noexcept override {
    return std::apply(
        [this](const DeferredArgument<Args>&... arguments) {
          return sizeof(*this) + TextSize(format_) +
                 (std::size_t{0} + ... + TextSize(arguments));
        },
        arguments_);
  }

 private:
  template <class T>
  static std::size_t TextSize(const T& argument) noexcept {
    if constexpr (std::is_same_v<T, std::string> ||
                  std::is_same_v<T, std::wstring>) {
      return argument.size() * sizeof(typename T::value_type);
    } else {
      return 0;
    }
  }

  A attributes_;
  decltype(std::declval<const A&>().Erase()) erased_;
  std::basic_string<CharT> format_;
  std::tuple<DeferredArgument<Args>...> arguments_;
};

template <class T>
inline constexpr bool kThrottles = HasThrottle<T>;

template <class... As>
inline constexpr bool kThrottles<Attributes<As...>> = (HasThrottle<As> || ...);

// Whether a set that skips the log file has a sink to send the message to.
template <class... As>
inline constexpr bool kHasDestination =
    !(std::is_same_v<As, LogFileExclusion> || ...) ||
    (std::is_same_v<As, ToSink> || ...);

// Whether a set stores the text in strings or vectors, which the caller reads
// once the call returns.
template <class... As>
inline constexpr bool kStoresText =
    ((std::is_same_v<As, ToString> || std::is_same_v<As, ToVector>) || ...);

// Formats that may consist of wide characters, unlike the empty one of a
// message without text.
template <class F>
concept WideCapableFormat = requires(const F& format) {
  { format.wide() } -> std::same_as<bool>;
  { format.get_wide() } -> std::convertible_to<std::wstring_view>;
};

// Logs a message of a format string of narrow or wide characters, on the
// logging thread if the attributes include Async.
template <class CharT, class... As, class... Args>
void WriteText(const Attributes<As...>& attributes,
               std::span<const ErasedAttribute> erased,
               const CallerConfiguration& configuration,
               const std::source_location& location,
               std::basic_string_view<CharT> format, Args&... args) {
  if constexpr (kAsync<Attributes<As...>>) {
    static_assert(!(kLazy<std::remove_cvref_t<Args>> || ...),
                  "Async cannot defer a Lazy argument, since its callable may "
                  "refer to state of the caller that ends before the logging "
                  "thread formats the message.");
    using Message = DeferredMessageOf<CharT, Attributes<As...>, Args...>;
    const auto copy = [&attributes, format, &args...] {
      return std::make_unique<Message>(attributes, format, args...);
    };
    using Copy = decltype(copy);
    DispatchDeferred(
        erased, configuration, location,
        MessageCopier{
            &copy, [](const void* message) -> std::unique_ptr<DeferredMessage> {
              return (*static_cast<const Copy*>(message))();
            }});
  } else {
    Dispatch(erased, configuration, location, format,
             std::make_format_args<
                 std::conditional_t<std::is_same_v<CharT, wchar_t>,
                                    std::wformat_context, std::format_context>>(
                 args...));
  }
}

template <class... As, class F, class... Args>
void Write(const Attributes<As...>& attributes,
           const CallerConfiguration& configuration, const F& format,
           Args&... args) {
  static_assert(kHasDestination<As...>,
                "SkipLogFile requires a ToSink destination, since the message "
                "would not be logged anywhere otherwise.");
  static_assert(!(kAsync<Attributes<As...>> && kStoresText<As...>),
                "Async cannot store the text in a ToString or ToVector "
                "destination, since the caller reads it before the logging "
                "thread writes it.");
  const auto erased = attributes.Erase();
  if constexpr (WideCapableFormat<F> && (Formattable<Args, wchar_t> && ...)) {
    if (format.wide()) {
      WriteText(attributes, erased, configuration, format.location(),
                format.get_wide(), args...);
      return;
    }
  }
  if constexpr ((Formattable<Args, char> && ...)) {
    WriteText(attributes, erased, configuration, format.location(),
              format.get(), args...);
  }
}

template <class... Args>
struct LogFormatFor<false, Args...> {
  using type = FormatString<std::type_identity_t<Args>...>;
};

// Checks and writes the messages of the regular destinations.
template <class A>
struct LogTraits<A, false> {
  static constexpr bool kAttributes = true;

  template <class... Args>
  static constexpr bool kArguments = true;

  // Writes the message. The prefix of a failed check is written by an
  // attribute instead.
  template <class... As, class F, class... Args>
  static void Emit(const Attributes<As...>& attributes,
                   const CallerConfiguration& configuration, const F& format,
                   std::string_view /*prefix*/, Args&... args) {
    Write(attributes, configuration, format, args...);
  }
};

}  // namespace internal

// Determines whether attributes admit a message at the calling site. Only the
// Configure() and Admit() hooks run. The minimum log level and stripping are
// not considered, so `IsEnabled(Verbose{n})` behaves like `VLOG_IS_ON(n)`.
//
// Attributes that throttle messages are rejected since evaluating them would
// count an occurrence that is never logged.
template <int StripLevel = NGLOG_STRIP_LOG,
          bool DebugMode = NGLOG_DCHECK_IS_ON(), AttributeSet A>
  requires(!internal::kThrottles<A>)
bool IsEnabled(const A& attributes, const std::source_location& location =
                                        std::source_location::current()) {
  // Keeps a set created for a single attribute alive while it is erased.
  const auto& set = internal::AsAttributes(attributes);
  const auto erased = set.Erase();
  return internal::IsAdmitted(erased, {StripLevel, DebugMode}, location);
}

}  // namespace nglog::experimental

#endif  // defined(NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_SUPPORTED)

#endif  // NGLOG_EXPERIMENTAL_LOG_H
