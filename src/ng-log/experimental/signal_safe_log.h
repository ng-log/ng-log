// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Support for the SignalSafe attribute, with which Log() and Ensure() write to
// stderr like RAW_LOG() without allocating memory or acquiring locks.

#ifndef NGLOG_EXPERIMENTAL_SIGNAL_SAFE_LOG_H
#define NGLOG_EXPERIMENTAL_SIGNAL_SAFE_LOG_H

#include "ng-log/experimental/requirements.h"  // IWYU pragma: keep

#if defined(NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_SUPPORTED)

#  include <algorithm>
#  include <array>
#  include <concepts>
#  include <cstddef>
#  include <format>
#  include <limits>
#  include <memory>
#  include <source_location>
#  include <span>
#  include <string>
#  include <string_view>
#  include <tuple>
#  include <type_traits>
#  include <version>

#  include "ng-log/experimental/attributes.h"
#  include "ng-log/experimental/format.h"
#  include "ng-log/experimental/log_call.h"  // IWYU pragma: export
#  include "ng-log/export.h"
#  include "ng-log/internal/utf8_encoder.h"

namespace nglog::experimental {

// Writes the message to stderr like RAW_LOG() without allocating memory or
// acquiring locks, for use in signal handlers and low-level code. Only a
// severity, Debug and Verbose combine with it, and the arguments are limited to
// integers, floating-point numbers, characters, booleans, pointers, C strings,
// std::string_view and std::string.
//
// With iterator debugging of the Microsoft C++ standard library, std::format
// allocates once for each integer formatted with a format specification, and
// for each character or boolean with an integer presentation type.
struct SignalSafeDestination {
  using Category = internal::DestinationCategory;

  // The destination is selected at compile time, so configuring has no effect.
  constexpr void Configure(Context& /*context*/) const noexcept {}
};

inline constexpr SignalSafeDestination SignalSafe{};

namespace internal {

// Formats a signal-safe message into a buffer of size bytes with the semantics
// of std::snprintf and returns the length of the complete text.
using SignalSafeMessageFormatter = int (*)(void* message, char* buffer,
                                           std::size_t size);

NGLOG_EXPORT void SignalSafeDispatch(
    std::span<const ErasedAttribute> attributes,
    const CallerConfiguration& configuration,
    const std::source_location& location, SignalSafeMessageFormatter format,
    void* message);

// Not constexpr, so that calling it while checking a format string at compile
// time names the reason the format string is rejected. Format strings are
// parsed again at runtime only after they passed that check.
inline void LocaleSpecificFormattingIsNotSignalSafe() noexcept {}

template <class CharT>
constexpr bool IsAlignment(CharT c) noexcept {
  return c == '<' || c == '>' || c == '^';
}

// Rejects the locale-specific option `L` of replacement fields.
template <class CharT>
consteval void RejectLocaleSpecificFormatting(
    std::basic_string_view<CharT> format) {
  for (std::size_t i = 0; i < format.size(); ++i) {
    if (format[i] != '{') {
      continue;
    }
    if (i + 1 < format.size() && format[i + 1] == '{') {
      ++i;
      continue;
    }
    // Finds the closing brace while skipping nested replacement fields.
    std::size_t depth = 1;
    std::size_t spec = std::basic_string_view<CharT>::npos;
    std::size_t j = i + 1;
    for (; j < format.size() && depth != 0; ++j) {
      if (format[j] == '{') {
        ++depth;
      } else if (format[j] == '}') {
        --depth;
      } else if (format[j] == ':' && depth == 1 &&
                 spec == std::basic_string_view<CharT>::npos) {
        spec = j + 1;
      }
    }
    if (spec != std::basic_string_view<CharT>::npos) {
      const std::size_t end = j - 1;
      std::size_t k = spec;
      // A fill character, which may be L, precedes an alignment.
      if (k + 1 < end && IsAlignment(format[k + 1])) {
        k += 2;
      }
      for (; k < end; ++k) {
        if (format[k] == 'L') {
          LocaleSpecificFormattingIsNotSignalSafe();
        }
      }
    }
    i = j - 1;
  }
}

// Floating-point numbers are formatted using std::to_chars, which never
// allocates. libc++ provides it for floating-point numbers without defining
// __cpp_lib_to_chars, since it lacks the floating-point std::from_chars. Its
// availability macro accounts for the deployment target on Apple platforms.
#  if (defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L) || \
      (defined(_LIBCPP_AVAILABILITY_HAS_TO_CHARS_FLOATING_POINT) &&     \
       _LIBCPP_AVAILABILITY_HAS_TO_CHARS_FLOATING_POINT)
inline constexpr bool kSignalSafeFloats = true;
#  else
inline constexpr bool kSignalSafeFloats = false;
#  endif

// Floating-point types FormatSignalSafeFloat() supports. Extended
// floating-point types, such as std::float16_t, are excluded.
template <class T>
inline constexpr bool kSupportedFloat =
    std::is_same_v<T, float> || std::is_same_v<T, double> ||
    std::is_same_v<T, long double>;

template <class T>
inline constexpr bool kSignalSafeFormattable =
    std::integral<T> || (kSignalSafeFloats && kSupportedFloat<T>) ||
    std::same_as<T, std::nullptr_t> || std::same_as<T, const void*> ||
    std::same_as<T, void*> || std::same_as<T, const char*> ||
    std::same_as<T, char*> || std::same_as<T, std::string_view> ||
    std::same_as<T, std::string> || std::same_as<T, const wchar_t*> ||
    std::same_as<T, wchar_t*> || std::same_as<T, std::wstring_view> ||
    std::same_as<T, std::wstring>;

// Types whose formatting neither allocates nor acquires locks.
template <class T>
concept SignalSafeFormattable =
    kSignalSafeFormattable<std::remove_cv_t<std::decay_t<T>>>;

// Number of code units a signal-safe message of a wide format string is
// formatted into before it is encoded as UTF-8. Longer messages are too long
// like messages that do not fit the buffer of RAW_LOG().
inline constexpr std::size_t kSignalSafeWideLength = 1024;

// Largest precision of a signal-safe floating-point argument.
inline constexpr int kMaxSignalSafeFloatPrecision = 64;

// Size of the buffer a signal-safe floating-point argument is formatted into.
inline constexpr std::size_t kSignalSafeFloatBufferSize = 512;

// Largest width of a signal-safe floating-point argument.
inline constexpr std::size_t kMaxSignalSafeFloatWidth =
    kSignalSafeFloatBufferSize;

// Base of the decimal digits of widths and precisions.
inline constexpr int kDecimalBase = 10;

// Largest code unit of ASCII.
inline constexpr unsigned char kMaxAscii = 0x7f;

// The fixed notation of a double with the largest precision fits the buffer:
// a sign, the integral digits, a decimal point and the fractional digits.
static_assert(1 + std::numeric_limits<double>::max_exponent10 + 1 + 1 +
                      kMaxSignalSafeFloatPrecision <=
                  kSignalSafeFloatBufferSize,
              "the longest fixed notation of a double with the largest "
              "precision must fit the buffer");

// Format specification of a signal-safe floating-point argument, a subset of
// the standard specification that can be formatted without allocating.
struct SignalSafeFloatSpec {
  char fill = ' ';
  // One of '<', '>' and '^', or '\0' for the default.
  char align = '\0';
  // One of '+', '-' and ' '.
  char sign = '-';
  bool zero_padding = false;
  std::size_t width = 0;
  // Negative if absent.
  int precision = -1;
  // One of "aAeEfFgG", or '\0' for the default.
  char type = '\0';
};

// Not constexpr, so that calling them while checking a format string at compile
// time names the reason the format string is rejected.
inline void SignalSafeFloatAlternateFormIsNotSupported() noexcept {}
inline void SignalSafeFloatDynamicFieldIsNotSupported() noexcept {}
inline void SignalSafeFloatPrecisionExceedsLimit() noexcept {}
inline void SignalSafeFloatWidthExceedsLimit() noexcept {}
inline void SignalSafeFloatWidthIsInvalid() noexcept {}
inline void SignalSafeFloatFillIsInvalid() noexcept {}
inline void SignalSafeFloatFillIsNotAscii() noexcept {}
inline void SignalSafeFloatPrecisionIsMissing() noexcept {}
inline void SignalSafeFloatTypeIsNotSupported() noexcept {}
inline void SignalSafeLongDoubleFixedNotationIsNotSupported() noexcept {}

template <class CharT>
constexpr bool IsDigit(CharT c) noexcept {
  return c >= '0' && c <= '9';
}

template <class CharT>
constexpr bool IsAscii(CharT c) noexcept {
  return static_cast<std::make_unsigned_t<CharT>>(c) <= kMaxAscii;
}

// Parses the specification of a signal-safe floating-point argument up to the
// closing brace. Fixed notation is rejected if it may exceed the buffer, which
// is the case for long double. Characters are compared before they are
// narrowed, so that a wide character never narrows to a valid one.
template <class T, class CharT>
constexpr const CharT* ParseSignalSafeFloatSpec(const CharT* it,
                                                const CharT* end,
                                                SignalSafeFloatSpec& spec) {
  if (it == end || *it == '}') {
    return it;
  }
  // Padding must fit the buffer, so a fill character takes a single byte.
  if (!IsAscii(*it)) {
    SignalSafeFloatFillIsNotAscii();
  }
  if (it + 1 != end && IsAlignment(it[1])) {
    if (*it == '{' || *it == '}') {
      SignalSafeFloatFillIsInvalid();
    }
    spec.fill = static_cast<char>(*it);
    spec.align = static_cast<char>(it[1]);
    it += 2;
  } else if (IsAlignment(*it)) {
    spec.align = static_cast<char>(*it);
    ++it;
  }
  if (it != end && (*it == '+' || *it == '-' || *it == ' ')) {
    spec.sign = static_cast<char>(*it);
    ++it;
  }
  if (it != end && *it == '#') {
    SignalSafeFloatAlternateFormIsNotSupported();
  }
  if (it != end && *it == '0') {
    spec.zero_padding = true;
    ++it;
  }
  if (it != end && *it == '{') {
    SignalSafeFloatDynamicFieldIsNotSupported();
  }
  if (it != end && *it == '0') {
    SignalSafeFloatWidthIsInvalid();
  }
  for (; it != end && IsDigit(*it); ++it) {
    spec.width =
        (spec.width * kDecimalBase) + static_cast<std::size_t>(*it - '0');
    if (spec.width > kMaxSignalSafeFloatWidth) {
      SignalSafeFloatWidthExceedsLimit();
    }
  }
  if (it != end && *it == '.') {
    ++it;
    if (it != end && *it == '{') {
      SignalSafeFloatDynamicFieldIsNotSupported();
    }
    if (it == end || !IsDigit(*it)) {
      SignalSafeFloatPrecisionIsMissing();
    }
    spec.precision = 0;
    for (; it != end && IsDigit(*it); ++it) {
      spec.precision = (spec.precision * kDecimalBase) + (*it - '0');
      if (spec.precision > kMaxSignalSafeFloatPrecision) {
        SignalSafeFloatPrecisionExceedsLimit();
      }
    }
  }
  if (it != end && *it == 'L') {
    LocaleSpecificFormattingIsNotSignalSafe();
  }
  if (it != end && *it != '}') {
    const std::string_view types = "aAeEfFgG";
    if (!IsAscii(*it) ||
        types.find(static_cast<char>(*it)) == std::string_view::npos) {
      SignalSafeFloatTypeIsNotSupported();
    }
    spec.type = static_cast<char>(*it);
    ++it;
  }
  if (it != end && *it != '}') {
    SignalSafeFloatTypeIsNotSupported();
  }
  if constexpr (std::is_same_v<T, long double>) {
    if (spec.type == 'f' || spec.type == 'F') {
      SignalSafeLongDoubleFixedNotationIsNotSupported();
    }
  }
  return it;
}

// Formats a signal-safe floating-point argument like std::format formats it
// into buffer of kSignalSafeFloatBufferSize bytes. Returns the length of the
// text. Neither allocates nor acquires locks.
NGLOG_EXPORT std::size_t FormatSignalSafeFloat(const SignalSafeFloatSpec& spec,
                                               float value,
                                               char* buffer) noexcept;
NGLOG_EXPORT std::size_t FormatSignalSafeFloat(const SignalSafeFloatSpec& spec,
                                               double value,
                                               char* buffer) noexcept;
NGLOG_EXPORT std::size_t FormatSignalSafeFloat(const SignalSafeFloatSpec& spec,
                                               long double value,
                                               char* buffer) noexcept;

// Signal-safe floating-point argument.
template <class T>
struct SignalSafeFloat {
  T value;
};

// Type a signal-safe argument is formatted as.
template <class T>
using SignalSafeArgumentType =
    std::conditional_t<std::is_floating_point_v<std::remove_cvref_t<T>>,
                       SignalSafeFloat<std::remove_cvref_t<T>>, T>;

// Type the format string of a signal-safe message is checked against. Passing
// every argument as a constant lvalue lets std::format_to_n() deduce exactly
// this type.
template <class T>
using SignalSafeFormatArgument =
    const std::remove_cvref_t<SignalSafeArgumentType<T>>&;

// Prefix, format string and wrapped arguments of a signal-safe message.
template <class... Ws>
struct SignalSafeMessage {
  std::string_view prefix;
  std::format_string<const Ws&...> format;
  std::tuple<const Ws&...> arguments;

  // Unlike std::vformat_to() into another output iterator, which libc++
  // buffers on the heap, std::format_to_n() writes through a pointer directly
  // and never allocates.
  static int Format(void* context, char* buffer, std::size_t size) {
    const auto& message = *static_cast<const SignalSafeMessage*>(context);
    // Reserves the last byte for the terminating null character.
    const auto capacity =
        static_cast<std::ptrdiff_t>(size == 0 ? size : size - 1);
    const auto prefix_size = static_cast<std::ptrdiff_t>(message.prefix.size());
    const std::ptrdiff_t copied = std::min(prefix_size, capacity);
    const auto result = std::apply(
        [&message, buffer, capacity, copied](const Ws&... arguments) {
          return std::format_to_n(
              std::ranges::copy_n(message.prefix.data(), copied, buffer).out,
              capacity - copied, message.format, arguments...);
        },
        message.arguments);
    if (size != 0) {
      *result.out = '\0';
    }
    constexpr std::ptrdiff_t kMaxLength = std::numeric_limits<int>::max();
    return static_cast<int>(std::min(prefix_size + result.size, kMaxLength));
  }
};

// Prefix, wide format string and wrapped arguments of a signal-safe message.
// std::format_to_n() writes through a pointer directly only if the pointer
// refers to the character type of the format string, so the message is
// formatted into wide characters first and then encoded as UTF-8.
template <class... Ws>
struct SignalSafeWideMessage {
  std::string_view prefix;
  std::wformat_string<const Ws&...> format;
  std::tuple<const Ws&...> arguments;

  // Returns the length of the text if it fits, and size otherwise, since only
  // the encoding of the formatted part of a longer text is known.
  static int Format(void* context, char* buffer, std::size_t size) {
    const auto& message = *static_cast<const SignalSafeWideMessage*>(context);
    std::array<wchar_t, kSignalSafeWideLength> wide;
    const auto result = std::apply(
        [&message, &wide](const Ws&... arguments) {
          return std::format_to_n(wide.data(),
                                  static_cast<std::ptrdiff_t>(wide.size()),
                                  message.format, arguments...);
        },
        message.arguments);
    // Reserves the last byte for the terminating null character.
    const std::size_t capacity = size == 0 ? size : size - 1;
    std::size_t length = std::min(message.prefix.size(), capacity);
    std::ranges::copy_n(message.prefix.data(),
                        static_cast<std::ptrdiff_t>(length), buffer);
    bool fits = length == message.prefix.size() &&
                result.size <= static_cast<std::ptrdiff_t>(wide.size());
    nglog::internal::Utf8Encoder encoder;
    std::array<char, nglog::internal::Utf8Encoder::kMaximumLength> encoded;
    // Copies whole code points only.
    const auto append = [buffer, capacity, &length, &fits,
                         &encoded](std::size_t encoded_length) {
      if (length + encoded_length > capacity) {
        fits = false;
        return;
      }
      std::ranges::copy_n(encoded.data(),
                          static_cast<std::ptrdiff_t>(encoded_length),
                          buffer + length);
      length += encoded_length;
    };
    for (const wchar_t* it = wide.data(); it != result.out && fits; ++it) {
      append(encoder.Put(*it, encoded.data()));
    }
    if (fits) {
      append(encoder.Finish(encoded.data()));
    }
    if (size != 0) {
      buffer[length] = '\0';
    }
    constexpr std::size_t kMaxLength = std::numeric_limits<int>::max();
    return static_cast<int>(std::min(fits ? length : size, kMaxLength));
  }
};

template <class T>
constexpr decltype(auto) WrapSignalSafeArgument(const T& value) noexcept {
  if constexpr (std::is_floating_point_v<T>) {
    return SignalSafeFloat<T>{value};
  } else {
    return (value);
  }
}

// Attributes that combine with SignalSafe. They neither allocate nor acquire
// locks except Verbose, which does so on the first call of a call site like
// RAW_VLOG(), and on every call at call sites that do not fit a lookup table
// of 4096 locations.
template <class T>
inline constexpr bool kSignalSafeAttribute =
    std::is_same_v<T, Severity> || std::is_same_v<T, DebugOnly> ||
    std::is_same_v<T, SignalSafeDestination>;

template <class T>
inline constexpr bool kSignalSafeAttribute<Verbose<T>> = true;

template <class... As>
inline constexpr bool kSignalSafeAttribute<Attributes<As...>> =
    (kSignalSafeAttribute<As> && ...);

}  // namespace internal

// Format string of a signal-safe message that additionally rejects
// locale-specific formatting, which may allocate and acquire locks.
template <class... Args>
class SignalSafeFormatString : public FormatString<Args...> {
 public:
  template <class T>
    requires std::convertible_to<const T&, std::string_view> ||
             std::convertible_to<const T&, std::wstring_view>
  consteval SignalSafeFormatString(
      const T& format,
      std::source_location location = std::source_location::current())
      : FormatString<Args...>{format, location} {
    if (this->wide()) {
      internal::RejectLocaleSpecificFormatting(this->get_wide());
    } else {
      internal::RejectLocaleSpecificFormatting(this->get());
    }
  }
};

namespace internal {

template <>
inline constexpr bool kSignalSafe<SignalSafeDestination> = true;

// Checks the format string against the arguments as they are formatted without
// allocating.
template <class... Args>
struct LogFormatFor<true, Args...> {
  using type = SignalSafeFormatString<SignalSafeFormatArgument<Args>...>;
};

// Writes the message like RAW_LOG() without allocating.
template <class A>
struct LogTraits<A, true> {
  static_assert(!kAsync<A>,
                "SignalSafe cannot be combined with Async, since passing a "
                "message to the logging thread allocates and acquires a lock, "
                "and a message still pending when a signal terminates the "
                "program is lost.");

  static constexpr bool kAttributes = kSignalSafeAttribute<A>;

  template <class... Args>
  static constexpr bool kArguments = (SignalSafeFormattable<Args> && ...);

  // Writes the message after the prefix. Floating-point arguments are
  // formatted using std::to_chars.
  template <class... As, class... Fs, class... Args>
  static void Emit(const Attributes<As...>& attributes,
                   const CallerConfiguration& configuration,
                   const SignalSafeFormatString<Fs...>& format,
                   std::string_view prefix, const Args&... args) {
    const auto erased = attributes.Erase();
    // A single lambda handles both kinds of format strings, since gcov gives
    // the lambdas of a function the same name.
    [&erased, &configuration, &format, prefix](const auto&... wrapped) {
      if (format.wide()) {
        if constexpr ((Formattable<Fs, wchar_t> && ...)) {
          using SignalSafeMessageType =
              SignalSafeWideMessage<std::remove_cvref_t<decltype(wrapped)>...>;
          SignalSafeMessageType message{
              prefix, format.checked_wide(), {wrapped...}};
          SignalSafeDispatch(erased, configuration, format.location(),
                             &SignalSafeMessageType::Format, &message);
        }
      } else if constexpr ((Formattable<Fs, char> && ...)) {
        using SignalSafeMessageType =
            SignalSafeMessage<std::remove_cvref_t<decltype(wrapped)>...>;
        SignalSafeMessageType message{prefix, format.checked(), {wrapped...}};
        SignalSafeDispatch(erased, configuration, format.location(),
                           &SignalSafeMessageType::Format, &message);
      }
    }(WrapSignalSafeArgument(args)...);
  }
};

}  // namespace internal

}  // namespace nglog::experimental

// Formats signal-safe floating-point arguments using std::to_chars. Supports
// fill, alignment, sign, zero padding, width, a precision of at most 64 and
// the types a, e, f, g and their uppercase variants.
template <class T, class CharT>
struct std::formatter<nglog::experimental::internal::SignalSafeFloat<T>,
                      CharT> {
  constexpr typename std::basic_format_parse_context<CharT>::iterator parse(
      std::basic_format_parse_context<CharT>& context) {
    return context.begin() +
           (nglog::experimental::internal::ParseSignalSafeFloatSpec<T>(
                std::to_address(context.begin()),
                std::to_address(context.end()), spec_) -
            std::to_address(context.begin()));
  }

  // The text consists of ASCII characters, which widen unchanged.
  template <class Context>
  typename Context::iterator format(
      const nglog::experimental::internal::SignalSafeFloat<T>& argument,
      Context& context) const {
    char buffer[nglog::experimental::internal::kSignalSafeFloatBufferSize];
    const std::size_t length =
        nglog::experimental::internal::FormatSignalSafeFloat(
            spec_, argument.value, buffer);
    return std::ranges::copy_n(buffer, static_cast<std::ptrdiff_t>(length),
                               context.out())
        .out;
  }

 private:
  nglog::experimental::internal::SignalSafeFloatSpec spec_;
};

#endif  // defined(NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_SUPPORTED)

#endif  // NGLOG_EXPERIMENTAL_SIGNAL_SAFE_LOG_H
