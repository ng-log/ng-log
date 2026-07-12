// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_COLOR_SPEC_H
#define NGLOG_INTERNAL_COLOR_SPEC_H

#include <cstddef>

#include "ng-log/internal/color.h"
#include "ng-log/internal/text_style.h"

namespace nglog {
namespace internal {

//! A complete replacement style for one text span.
struct ColorSpec {
  ColorValue foreground;
  TextStyle style = TextStyle::kNone;
  ColorValue background;

  void FormatAnsiSequence(char* out, std::size_t out_size) const;
};

inline bool operator==(const ColorSpec& lhs, const ColorSpec& rhs) noexcept {
  return lhs.foreground == rhs.foreground && lhs.style == rhs.style &&
         lhs.background == rhs.background;
}

inline bool operator!=(const ColorSpec& lhs, const ColorSpec& rhs) noexcept {
  return !(lhs == rhs);
}

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_COLOR_SPEC_H
