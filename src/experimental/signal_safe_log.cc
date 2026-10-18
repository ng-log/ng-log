// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Formats and dispatches raw messages without allocating memory or acquiring
// locks.

#include "ng-log/experimental/signal_safe_log.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iterator>
#include <source_location>
#include <string_view>
#include <system_error>

#include "context.h"
#include "internal/raw_log.h"
#include "ng-log/experimental/attributes.h"
#include "ng-log/log_severity.h"

namespace nglog::experimental::internal {
namespace {

// Returns the default precision of a type, which std::format uses as well.
constexpr int kDefaultFloatPrecision = 6;

template <class T>
std::to_chars_result ToChars(const SignalSafeFloatSpec& spec, T value,
                             char* first, char* last) noexcept {
  const bool has_precision = spec.precision >= 0;
  const int precision = has_precision ? spec.precision : kDefaultFloatPrecision;
  switch (spec.type) {
    case 'a':
    case 'A':
      return has_precision
                 ? std::to_chars(first, last, value, std::chars_format::hex,
                                 precision)
                 : std::to_chars(first, last, value, std::chars_format::hex);
    case 'e':
    case 'E':
      return std::to_chars(first, last, value, std::chars_format::scientific,
                           precision);
    case 'f':
    case 'F':
      return std::to_chars(first, last, value, std::chars_format::fixed,
                           precision);
    case 'g':
    case 'G':
      return std::to_chars(first, last, value, std::chars_format::general,
                           precision);
  }
  // Without a type, the shortest representation unless a precision is given.
  return has_precision ? std::to_chars(first, last, value,
                                       std::chars_format::general, precision)
                       : std::to_chars(first, last, value);
}

constexpr char ToUpperAscii(char c) noexcept {
  return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

template <class T>
std::size_t FormatSignalSafeFloatImpl(const SignalSafeFloatSpec& spec, T value,
                                      char* buffer) noexcept {
  char digits[kSignalSafeFloatBufferSize];
  const std::to_chars_result result =
      ToChars(spec, value, std::begin(digits), std::end(digits));
  if (result.ec != std::errc{}) {
    // The specification is limited such that the text always fits.
    return 0;
  }

  using std::isfinite;
  using std::isnan;
  using std::signbit;

  const char* body = digits;
  const char* body_end = result.ptr;
  bool negative = false;
  if (isnan(value)) {
    // Some standard libraries, such as libc++, describe the payload of NaN in
    // std::to_chars, as in -nan(ind), unlike std::format.
    constexpr std::string_view kNan = "nan";
    body = kNan.data();
    body_end = kNan.data() + kNan.size();
    negative = signbit(value);
  } else if (*body == '-') {
    negative = true;
    ++body;
  }
  char sign = '\0';
  if (negative) {
    sign = '-';
  } else if (spec.sign == '+' || spec.sign == ' ') {
    sign = spec.sign;
  }

  const bool uppercase = spec.type == 'A' || spec.type == 'E' ||
                         spec.type == 'F' || spec.type == 'G';
  const auto length =
      static_cast<std::size_t>(body_end - body) + (sign == '\0' ? 0 : 1);
  const std::size_t padding = spec.width > length ? spec.width - length : 0;

  char* out = buffer;
  const auto write_body = [&out, body, body_end, uppercase] {
    out = std::ranges::transform(body, body_end, out, [uppercase](char c) {
            return uppercase ? ToUpperAscii(c) : c;
          }).out;
  };

  // Like std::format, zero padding goes after the sign and does not apply to
  // an explicit alignment, an infinity or NaN.
  if (spec.zero_padding && spec.align == '\0' && isfinite(value)) {
    if (sign != '\0') {
      *out++ = sign;
    }
    out = std::ranges::fill_n(out, static_cast<std::ptrdiff_t>(padding), '0');
    write_body();
    return static_cast<std::size_t>(out - buffer);
  }

  // Numbers are right aligned by default.
  std::size_t before = padding;
  if (spec.align == '<') {
    before = 0;
  } else if (spec.align == '^') {
    before = padding / 2;
  }
  out =
      std::ranges::fill_n(out, static_cast<std::ptrdiff_t>(before), spec.fill);
  if (sign != '\0') {
    *out++ = sign;
  }
  write_body();
  out = std::ranges::fill_n(out, static_cast<std::ptrdiff_t>(padding - before),
                            spec.fill);
  return static_cast<std::size_t>(out - buffer);
}

}  // namespace

std::size_t FormatSignalSafeFloat(const SignalSafeFloatSpec& spec, float value,
                                  char* buffer) noexcept {
  return FormatSignalSafeFloatImpl(spec, value, buffer);
}

std::size_t FormatSignalSafeFloat(const SignalSafeFloatSpec& spec, double value,
                                  char* buffer) noexcept {
  return FormatSignalSafeFloatImpl(spec, value, buffer);
}

std::size_t FormatSignalSafeFloat(const SignalSafeFloatSpec& spec,
                                  long double value, char* buffer) noexcept {
  return FormatSignalSafeFloatImpl(spec, value, buffer);
}

void SignalSafeDispatch(std::span<const ErasedAttribute> attributes,
                        const CallerConfiguration& configuration,
                        const std::source_location& location,
                        SignalSafeMessageFormatter format, void* message) {
  // Configure() and Admit() of the accepted attributes neither allocate nor
  // acquire locks, except Verbose on the first call of a call site.
  // A signal handler must not change the errno of the interrupted code, also
  // if writing the message fails.
  ContextImpl context{location, configuration.debug, errno};
  const ErrnoRestorer restore_errno{context};
  RunHooks(attributes, &Hooks::configure, context);

  if (!PassHooks(attributes, &Hooks::admit, context)) {
    return;
  }

  // Stripped fatal raw messages still terminate with the exit status of
  // RAW_LOG(FATAL) unless they are not admitted, such as debug only ones in
  // release mode. Unlike std::exit(), std::_Exit() runs neither exit handlers
  // nor static destructors, which are not async-signal-safe.
  if (context.severity() < configuration.strip_level) {
    if (context.severity() == NGLOG_FATAL) {
      std::_Exit(EXIT_FAILURE);
    }
    return;
  }

  nglog::internal::RawLog(context.severity(), location.file_name(),
                          static_cast<int>(location.line()), format, message);
}

}  // namespace nglog::experimental::internal
