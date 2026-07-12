// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_COLOR_H
#define NGLOG_INTERNAL_COLOR_H

#include <cstdint>

namespace nglog {
namespace internal {

//! A named terminal color. kDefault restores the surrounding terminal color.
enum class Color {
  kDefault,
  kBlack,
  kRed,
  kGreen,
  kYellow,
  kBlue,
  kMagenta,
  kCyan,
  kWhite,
  kGray,
  kBrightRed,
  kBrightGreen,
  kBrightYellow,
  kBrightBlue,
  kBrightMagenta,
  kBrightCyan,
  kBrightWhite,
};

//! An RGB color used when the terminal supports extended ANSI colors.
struct RgbColor {
  std::uint8_t red = 0;
  std::uint8_t green = 0;
  std::uint8_t blue = 0;
};

struct ColorValue {
  enum class Kind { kNamed, kRgb };

  constexpr ColorValue() noexcept
      : kind(Kind::kNamed), named(Color::kDefault) {}
  constexpr ColorValue(
      Color color) noexcept  // NOLINT(google-explicit-constructor)
      : kind(Kind::kNamed), named(color) {}
  constexpr ColorValue(
      RgbColor color) noexcept  // NOLINT(google-explicit-constructor)
      : kind(Kind::kRgb), rgb(color) {}

  Kind kind;
  union {
    Color named;
    RgbColor rgb;
  };
};

inline bool operator==(const ColorValue& lhs, const ColorValue& rhs) noexcept {
  if (lhs.kind != rhs.kind) {
    return false;
  }
  if (lhs.kind == ColorValue::Kind::kNamed) {
    return lhs.named == rhs.named;
  }
  return lhs.rgb.red == rhs.rgb.red && lhs.rgb.green == rhs.rgb.green &&
         lhs.rgb.blue == rhs.rgb.blue;
}

inline bool operator!=(const ColorValue& lhs, const ColorValue& rhs) noexcept {
  return !(lhs == rhs);
}

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_COLOR_H
