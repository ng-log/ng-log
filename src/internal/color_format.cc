// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include <cstddef>

#include "ng-log/internal/color_spec.h"

namespace nglog {
namespace internal {

namespace {

constexpr int kSgrBold = 1;
constexpr int kSgrDim = 2;
constexpr int kSgrItalic = 3;
constexpr int kSgrUnderline = 4;
constexpr int kSgrForegroundDefault = 39;
constexpr int kSgrBackgroundDefault = 49;
constexpr int kSgrForegroundBase = 30;
constexpr int kSgrForegroundBrightBase = 90;
constexpr int kSgrBackgroundBase = 40;
constexpr int kSgrBackgroundBrightBase = 100;
constexpr int kSgrForegroundExtended = 38;
constexpr int kSgrBackgroundExtended = 48;
constexpr int kSgrExtendedColorRgb = 2;
// SGR codes never exceed three decimal digits.
constexpr std::size_t kMaxSgrCodeDigits = 3;

int NamedColorOffset(Color color) {
  switch (color) {
    case Color::kDefault:
      return -1;
    case Color::kBlack:
    case Color::kGray:
      return 0;
    case Color::kRed:
    case Color::kBrightRed:
      return 1;
    case Color::kGreen:
    case Color::kBrightGreen:
      return 2;
    case Color::kYellow:
    case Color::kBrightYellow:
      return 3;
    case Color::kBlue:
    case Color::kBrightBlue:
      return 4;
    case Color::kMagenta:
    case Color::kBrightMagenta:
      return 5;
    case Color::kCyan:
    case Color::kBrightCyan:
      return 6;
    case Color::kWhite:
    case Color::kBrightWhite:
      return 7;
  }
  return -1;
}

bool IsBrightColor(Color color) {
  switch (color) {
    case Color::kDefault:
    case Color::kBlack:
    case Color::kRed:
    case Color::kGreen:
    case Color::kYellow:
    case Color::kBlue:
    case Color::kMagenta:
    case Color::kCyan:
    case Color::kWhite:
      return false;
    case Color::kGray:
    case Color::kBrightRed:
    case Color::kBrightGreen:
    case Color::kBrightYellow:
    case Color::kBrightBlue:
    case Color::kBrightMagenta:
    case Color::kBrightCyan:
    case Color::kBrightWhite:
      return true;
  }
  return false;
}

int NamedColorSgrCode(Color color, bool is_background) {
  const int offset = NamedColorOffset(color);
  if (offset < 0) {
    return is_background ? kSgrBackgroundDefault : kSgrForegroundDefault;
  }
  const int base = is_background
                       ? (IsBrightColor(color) ? kSgrBackgroundBrightBase
                                               : kSgrBackgroundBase)
                       : (IsBrightColor(color) ? kSgrForegroundBrightBase
                                               : kSgrForegroundBase);
  return base + offset;
}

// Writes the trailing decimal digits of the value that fit into the
// non-empty range [begin, end) so that they end at "end", and returns the
// position of the first digit.
char* FormatSmallUint(const char* const begin, char* end, unsigned value) {
  constexpr unsigned kDecimalRadix = 10;
  do {
    *--end = static_cast<char>('0' + (value % kDecimalRadix));
    value /= kDecimalRadix;
  } while (value != 0 && end != begin);
  return end;
}

}  // namespace

void ColorSpec::FormatAnsiSequence(char* out, std::size_t out_size) const {
  if (out_size == 0) {
    return;
  }

  // A truncated sequence would leave the terminal inside an unterminated
  // escape sequence that consumes the text after it, so the sequence is
  // written completely or not at all.
  char* cursor = out;
  char* const end = out + out_size - 1;
  bool fits = true;
  const auto append_char = [&cursor, &fits, end](char c) {
    if (cursor < end) {
      *cursor++ = c;
    } else {
      fits = false;
    }
  };
  bool first = true;
  const auto append_code = [&append_char, &first](int code) {
    if (!first) {
      append_char(';');
    }
    char digits[kMaxSgrCodeDigits];
    char* const digits_end = digits + sizeof(digits);
    for (const char* digit =
             FormatSmallUint(digits, digits_end, static_cast<unsigned>(code));
         digit != digits_end; ++digit) {
      append_char(*digit);
    }
    first = false;
  };
  const auto append_color = [&append_code](const ColorValue& value,
                                           bool is_background) {
    if (value.kind == ColorValue::Kind::kRgb) {
      append_code(is_background ? kSgrBackgroundExtended
                                : kSgrForegroundExtended);
      append_code(kSgrExtendedColorRgb);
      append_code(value.rgb.red);
      append_code(value.rgb.green);
      append_code(value.rgb.blue);
    } else {
      append_code(NamedColorSgrCode(value.named, is_background));
    }
  };

  append_char('\033');
  append_char('[');
  if ((style & TextStyle::kBold) != TextStyle::kNone) {
    append_code(kSgrBold);
  }
  if ((style & TextStyle::kDim) != TextStyle::kNone) {
    append_code(kSgrDim);
  }
  if ((style & TextStyle::kItalic) != TextStyle::kNone) {
    append_code(kSgrItalic);
  }
  if ((style & TextStyle::kUnderline) != TextStyle::kNone) {
    append_code(kSgrUnderline);
  }
  append_color(foreground, /*is_background=*/false);
  if (background != ColorValue(Color::kDefault)) {
    append_color(background, /*is_background=*/true);
  }
  append_char('m');
  *(fits ? cursor : out) = '\0';
}

}  // namespace internal
}  // namespace nglog
