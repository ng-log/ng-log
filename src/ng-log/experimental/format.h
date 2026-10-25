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

namespace internal {

template <class T>
using InvokeResult = std::remove_cvref_t<std::invoke_result_t<const T&>>;

// Types formatted by formatters for the character type. Disabled formatters
// cannot be default constructed.
template <class T, class CharT>
concept Formattable = std::is_default_constructible_v<
    std::formatter<std::remove_cvref_t<T>, CharT>>;

template <class F>
concept LazyFormattable =
    std::invocable<const F&> && !std::is_void_v<InvokeResult<F>> &&
    (Formattable<InvokeResult<F>, char> ||
     Formattable<InvokeResult<F>, wchar_t>);

// Stands in for a format string of a character type that some argument has no
// formatter for.
struct UncheckedFormat {};

// Format string of characters of type CharT checked against the argument types
// if each argument can be formatted with them.
template <class CharT, class... Args>
using CheckedFormat =
    std::conditional_t<(Formattable<Args, CharT> && ...),
                       std::basic_format_string<CharT, Args...>,
                       UncheckedFormat>;

}  // namespace internal

// Format string that captures where the logging call is located. A format
// string of wide characters is formatted by the formatters for wchar_t.
template <class... Args>
class FormatString {
 public:
  template <class T>
    requires std::convertible_to<const T&, std::string_view> ||
                 std::convertible_to<const T&, std::wstring_view>
  consteval FormatString(const T& format, std::source_location location =
                                              std::source_location::current())
      : checked_{Check<char>(format)},
        checked_wide_{Check<wchar_t>(format)},
        text_{TextOf<char>(format)},
        wide_text_{TextOf<wchar_t>(format)},
        location_{location},
        wide_{std::convertible_to<const T&, std::wstring_view>} {}

  // Whether the format string consists of wide characters.
  constexpr bool wide() const noexcept { return wide_; }
  // The format string of narrow characters, which is empty if wide() holds.
  constexpr std::string_view get() const noexcept { return text_; }
  // The format string of wide characters, which is empty unless wide() holds.
  constexpr std::wstring_view get_wide() const noexcept { return wide_text_; }
  // The format string checked against the argument types, which is empty if
  // wide() holds.
  constexpr const internal::CheckedFormat<char, Args...>& checked()
      const noexcept {
    return checked_;
  }
  // The wide format string checked against the argument types, which is empty
  // unless wide() holds.
  constexpr const internal::CheckedFormat<wchar_t, Args...>& checked_wide()
      const noexcept {
    return checked_wide_;
  }
  constexpr const std::source_location& location() const noexcept {
    return location_;
  }

 private:
  // Constructing the format string checks it at compile time. Unused arguments
  // are valid, so an empty format string checks against any arguments that can
  // be formatted.
  //
  // Both character types have a member instead of sharing a union, since MSVC
  // rejects the result of a consteval constructor holding a union whose active
  // member is not the first one, with error C2440 "cannot convert from
  // 'initializer list'". MSVC 19.29 through 19.51 fail to compile
  //
  //   union U {
  //     constexpr U(int v) : i{v} {}
  //     constexpr U(const char* v) : p{v} {}
  //     int i;
  //     const char* p;
  //   };
  //   struct S {
  //     consteval S(const char* v) : u{v} {}
  //     U u;
  //   };
  //   void F(S) {}
  //   void G() { F("x"); }
  //
  // which compiles once p precedes i or the constructor of S is constexpr.
  // A union of both checked format strings suffices once MSVC accepts it.
  template <class CharT, class T>
  static consteval internal::CheckedFormat<CharT, Args...> Check(
      const T& format) {
    if constexpr (std::is_same_v<internal::CheckedFormat<CharT, Args...>,
                                 internal::UncheckedFormat>) {
      return {};
    } else {
      return TextOf<CharT>(format);
    }
  }

  // The format string if it consists of characters of type CharT, or an empty
  // one otherwise.
  template <class CharT, class T>
  static consteval std::basic_string_view<CharT> TextOf(const T& format) {
    if constexpr (std::convertible_to<const T&,
                                      std::basic_string_view<CharT>>) {
      return format;
    } else {
      return {};
    }
  }

  internal::CheckedFormat<char, Args...> checked_;
  internal::CheckedFormat<wchar_t, Args...> checked_wide_;
  std::string_view text_;
  std::wstring_view wide_text_;
  std::source_location location_;
  bool wide_;
};

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
template <class F, class CharT>
struct std::formatter<nglog::experimental::Lazy<F>, CharT>
    : std::formatter<nglog::experimental::internal::InvokeResult<F>, CharT> {
  template <class Context>
  typename Context::iterator format(
      const nglog::experimental::Lazy<F>& argument, Context& context) const {
    return std::formatter<nglog::experimental::internal::InvokeResult<F>,
                          CharT>::format(std::invoke(argument.callable),
                                         context);
  }
};

#endif  // defined(NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_SUPPORTED)

#endif  // NGLOG_EXPERIMENTAL_FORMAT_H
