// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Format string of the experimental logging functions, checked at compile time
// like the one of std::format, which captures the location of the call, and
// lazily evaluated arguments.

#ifndef NGLOG_EXPERIMENTAL_FORMAT_H
#define NGLOG_EXPERIMENTAL_FORMAT_H

#include "ng-log/experimental/requirements.h"  // IWYU pragma: keep

#if defined(NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_SUPPORTED)

#  include <concepts>
#  include <format>
#  include <functional>
#  include <source_location>
#  include <string_view>
#  include <type_traits>
#  include <utility>

namespace nglog::experimental {

// Format string that captures where the logging call is located.
template <class... Args>
class FormatString {
 public:
  template <class T>
    requires std::convertible_to<const T&, std::string_view>
  consteval FormatString(const T& format, std::source_location location =
                                              std::source_location::current())
      : format_{format}, text_{format}, location_{location} {}

  constexpr std::string_view get() const noexcept { return text_; }
  // The format string checked against the argument types.
  constexpr const std::format_string<Args...>& checked() const noexcept {
    return format_;
  }
  constexpr const std::source_location& location() const noexcept {
    return location_;
  }

 private:
  // Constructing the format string checks it at compile time.
  std::format_string<Args...> format_;
  std::string_view text_;
  std::source_location location_;
};

namespace internal {

template <class T>
using InvokeResult = std::remove_cvref_t<std::invoke_result_t<const T&>>;

// Disabled formatters cannot be default constructed.
template <class F>
concept LazyFormattable =
    std::invocable<const F&> && !std::is_void_v<InvokeResult<F>> &&
    std::is_default_constructible_v<std::formatter<InvokeResult<F>, char>>;

}  // namespace internal

// Format argument whose callable runs each time the message formats a
// replacement field that refers to it. The callable therefore does not run for
// messages that are not logged.
template <class F>
  requires internal::LazyFormattable<F>
struct Lazy {
  constexpr explicit Lazy(F function) noexcept(
      std::is_nothrow_move_constructible_v<F>)
      : callable{std::move(function)} {}

  F callable;
};

}  // namespace nglog::experimental

// Formats the result of the callable of a lazy argument with its formatter.
template <class F>
struct std::formatter<nglog::experimental::Lazy<F>, char>
    : std::formatter<nglog::experimental::internal::InvokeResult<F>, char> {
  std::format_context::iterator format(
      const nglog::experimental::Lazy<F>& argument,
      std::format_context& context) const {
    return std::formatter<nglog::experimental::internal::InvokeResult<F>,
                          char>::format(std::invoke(argument.callable),
                                        context);
  }
};

#endif  // defined(NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_SUPPORTED)

#endif  // NGLOG_EXPERIMENTAL_FORMAT_H
