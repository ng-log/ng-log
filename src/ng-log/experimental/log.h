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

#  include <format>
#  include <source_location>
#  include <span>
#  include <string_view>
#  include <type_traits>

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

template <class T>
inline constexpr bool kThrottles = HasThrottle<T>;

template <class... As>
inline constexpr bool kThrottles<Attributes<As...>> = (HasThrottle<As> || ...);

// Whether a set that skips the log file has a sink to send the message to.
template <class... As>
inline constexpr bool kHasDestination =
    !(std::is_same_v<As, LogFileExclusion> || ...) ||
    (std::is_same_v<As, ToSink> || ...);

template <class... As, class... Args>
void Write(const Attributes<As...>& attributes,
           const CallerConfiguration& configuration, std::string_view format,
           const std::source_location& location, Args&... args) {
  static_assert(kHasDestination<As...>,
                "SkipLogFile requires a ToSink destination, since the message "
                "would not be logged anywhere otherwise.");
  const auto erased = attributes.Erase();
  Dispatch(erased, configuration, location, format,
           std::make_format_args(args...));
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
    Write(attributes, configuration, format.get(), format.location(), args...);
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
