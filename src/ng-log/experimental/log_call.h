// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Log, the logging function shared by the destinations. The regular
// destinations are available with ng-log/experimental/log.h and the signal-safe
// one with ng-log/experimental/signal_safe_log.h.

#ifndef NGLOG_EXPERIMENTAL_LOG_CALL_H
#define NGLOG_EXPERIMENTAL_LOG_CALL_H

#include "ng-log/experimental/requirements.h"  // IWYU pragma: keep

#if defined(NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_SUPPORTED)

#  include <concepts>
#  include <source_location>
#  include <string_view>
#  include <type_traits>
#  include <utility>

#  include "ng-log/experimental/attributes.h"
#  include "ng-log/experimental/format.h"
#  include "ng-log/log_message.h"

namespace nglog::experimental {

namespace internal {

// Whether attributes select the signal-safe destination, which
// ng-log/experimental/signal_safe_log.h provides.
template <class T>
inline constexpr bool kSignalSafe = false;

template <class... As>
inline constexpr bool kSignalSafe<Attributes<As...>> = (kSignalSafe<As> || ...);

// Format string of a message for the regular or the signal-safe destinations,
// which log.h and signal_safe_log.h define respectively. A nested type, unlike
// an alias template member of LogTraits, does not crash include-what-you-use.
template <bool kSignalSafeDestination, class... Args>
struct LogFormatFor {
  static_assert(kSignalSafeDestination,
                "Logging to the regular destinations requires including "
                "ng-log/experimental/log.h.");
};

// How the messages of attributes are checked and written. log.h specializes it
// for the regular destinations and signal_safe_log.h for the signal-safe one,
// so that each kind of message is only available with its header.
template <class A, bool = kSignalSafe<A>>
struct LogTraits {
  static_assert(kAlwaysFalse<A>,
                "Logging to the regular destinations requires including "
                "ng-log/experimental/log.h.");
};

// Format of a message without text, such as that of a failed comparison
// checked without a message, at the location of the call.
class EmptyFormat {
 public:
  constexpr explicit EmptyFormat(const std::source_location& location) noexcept
      : location_{location} {}

  constexpr std::string_view get() const noexcept { return {}; }
  constexpr const std::source_location& location() const noexcept {
    return location_;
  }

 private:
  std::source_location location_;
};

// Has no format string, so that substituting it fails.
struct NoLogFormat {};

// Format string of a message with the attributes A. Compilers that substitute
// the parameters of a deduction guide before checking its constraints, such as
// Clang 19, also ask for the format string of other types, such as a C string.
// Substitution then fails instead of requiring log.h.
template <class A, class... Args>
using LogFormatString = typename std::conditional_t<
    AttributeSet<A>, LogFormatFor<kSignalSafe<A>, Args...>, NoLogFormat>::type;

// SignalSafe combines only with attributes that neither allocate nor acquire
// locks.
template <class A>
concept SignalSafeAttributes = LogTraits<A>::kAttributes;

// The arguments of a signal-safe message are limited to types whose formatting
// neither allocates nor acquires locks.
template <class A, class... Args>
concept SignalSafeArguments = LogTraits<A>::template kArguments<Args...>;

// Writes a message with the attributes of a logging call.
template <class A, class... As, class F, class... Args>
void Emit(const Attributes<As...>& attributes,
          const CallerConfiguration& configuration, const F& format,
          Args&... args) {
  LogTraits<A>::Emit(attributes, configuration, format, {}, args...);
}

// Marks the logger a two-stage logging call creates.
struct Deferred;

template <class A>
using AttributesOf =
    std::remove_cvref_t<decltype(AsAttributes(std::declval<const A&>()))>;

// Attributes of a logging call. Converting them runs their Capture() hooks.
// The conversion initializes the first constructor parameter of Log, so it is
// sequenced before the remaining arguments of a braced call.
template <AttributeSet A>
class CapturedAttributes {
 public:
  constexpr CapturedAttributes(const A& attributes)
      : attributes_{AsAttributes(attributes).Capture()} {}

  constexpr const AttributesOf<A>& get() const noexcept { return attributes_; }

 private:
  AttributesOf<A> attributes_;
};

}  // namespace internal

// Whether debug mode is on in the calling translation unit like DCHECK_IS_ON(),
// in which Debug messages and checks are logged. Unlike Debug, it lets
// if constexpr skip code such as the arguments of an expensive check.
template <bool DebugMode = NGLOG_DCHECK_IS_ON()>
consteval bool IsDebugEnabled() noexcept {
  return DebugMode;
}

// Logs a message described by attributes when constructed, such as with
// `Log{attributes, format, args...}` or `Log(format, args...)`.
//
// Braces evaluate the attributes before the arguments, which MSVC 19.51 does
// not although the standard requires it. Only then do attributes capture state,
// such as errno, before an argument changes it. The template
// arguments are deduced and capture the configuration of the calling
// translation unit.
template <int StripLevel, bool DebugMode, AttributeSet A, class... Args>
class Log {
 public:
  constexpr Log(internal::CapturedAttributes<A> attributes,
                internal::LogFormatString<A, Args...> format, Args&&... args)
    requires(!internal::kUsesOccurrence<Args...> ||
             internal::kCountsOccurrences<A>) &&
            internal::SignalSafeAttributes<A> &&
            internal::SignalSafeArguments<A, Args...>
  {
    // Logging has no effect during constant evaluation.
    if (std::is_constant_evaluated()) {
      return;
    }
    internal::Emit<A>(attributes.get(), {StripLevel, DebugMode}, format,
                      args...);
  }

  // Logs an informational message.
  constexpr Log(FormatString<std::type_identity_t<Args>...> format,
                Args&&... args)
    requires(std::is_same_v<A, Severity> && !internal::kUsesOccurrence<Args...>)
  {
    // Logging has no effect during constant evaluation.
    if (std::is_constant_evaluated()) {
      return;
    }
    internal::Emit<Severity>(internal::AsAttributes(Info),
                             {StripLevel, DebugMode}, format, args...);
  }

  // Logs a message described by attributes only if the condition holds like
  // LOG_IF(). Unlike with an if statement, the arguments are evaluated even if
  // the condition does not hold.
  constexpr Log(bool condition, internal::CapturedAttributes<A> attributes,
                internal::LogFormatString<A, Args...> format, Args&&... args)
    requires(!internal::kUsesOccurrence<Args...> ||
             internal::kCountsOccurrences<A>) &&
            internal::SignalSafeAttributes<A> &&
            internal::SignalSafeArguments<A, Args...>
  {
    // Logging has no effect during constant evaluation.
    if (!condition || std::is_constant_evaluated()) {
      return;
    }
    internal::Emit<A>(attributes.get(), {StripLevel, DebugMode}, format,
                      args...);
  }

  // Logs an informational message only if the condition holds.
  constexpr Log(bool condition,
                FormatString<std::type_identity_t<Args>...> format,
                Args&&... args)
    requires(std::is_same_v<A, Severity> && !internal::kUsesOccurrence<Args...>)
  {
    // Logging has no effect during constant evaluation.
    if (!condition || std::is_constant_evaluated()) {
      return;
    }
    internal::Emit<Severity>(internal::AsAttributes(Info),
                             {StripLevel, DebugMode}, format, args...);
  }
};

// Logger of a two-stage call `Log(attributes)(format, args...)`. The logger is
// created before the arguments of the second stage are evaluated, so its
// attributes capture state before an argument changes it. Its call operator
// requires an rvalue, so that a logger is called as the temporary of that call.
// A stored logger must be moved to be called and would report state captured
// long before.
template <int StripLevel, bool DebugMode, AttributeSet A>
class Log<StripLevel, DebugMode, A, internal::Deferred> {
 public:
  [[nodiscard("call the logger with the format string and arguments")]]
  constexpr explicit Log(internal::CapturedAttributes<A> attributes)
      : attributes_{attributes} {}

  template <class... Args>
    requires(!internal::kUsesOccurrence<Args...> ||
             internal::kCountsOccurrences<A>) &&
            internal::SignalSafeAttributes<A> &&
            internal::SignalSafeArguments<A, Args...>
  constexpr void operator()(internal::LogFormatString<A, Args...> format,
                            Args&&... args) && {
    // Logging has no effect during constant evaluation.
    if (std::is_constant_evaluated()) {
      return;
    }
    internal::Emit<A>(attributes_.get(), {StripLevel, DebugMode}, format,
                      args...);
  }

 private:
  internal::CapturedAttributes<A> attributes_;
};

template <AttributeSet A, class... Args>
Log(const A&, internal::LogFormatString<A, Args...>, Args&&...)
    -> Log<NGLOG_STRIP_LOG, NGLOG_DCHECK_IS_ON(), A, Args...>;

template <class... Args>
Log(FormatString<std::type_identity_t<Args>...>, Args&&...)
    -> Log<NGLOG_STRIP_LOG, NGLOG_DCHECK_IS_ON(), Severity, Args...>;

template <AttributeSet A>
Log(const A&)
    -> Log<NGLOG_STRIP_LOG, NGLOG_DCHECK_IS_ON(), A, internal::Deferred>;

// A condition must be a bool. Otherwise, a C string format string would
// convert to a condition and make Log("{}", "text") ambiguous.
template <std::same_as<bool> Condition, AttributeSet A, class... Args>
Log(Condition, const A&, internal::LogFormatString<A, Args...>, Args&&...)
    -> Log<NGLOG_STRIP_LOG, NGLOG_DCHECK_IS_ON(), A, Args...>;

template <std::same_as<bool> Condition, class... Args>
Log(Condition, FormatString<std::type_identity_t<Args>...>, Args&&...)
    -> Log<NGLOG_STRIP_LOG, NGLOG_DCHECK_IS_ON(), Severity, Args...>;

}  // namespace nglog::experimental

#endif  // defined(NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_SUPPORTED)

#endif  // NGLOG_EXPERIMENTAL_LOG_CALL_H
