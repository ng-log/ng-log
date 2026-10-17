// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_COLOR_SPEC_H
#define NGLOG_INTERNAL_COLOR_SPEC_H

// IWYU pragma: private, include "ng-log/logging.h"
// IWYU pragma: friend "ng-log/internal/.*"

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

  // Writes the SGR escape sequence of the spec, or an empty string if the
  // complete sequence does not fit into out_size bytes.
  void FormatAnsiSequence(char* out, std::size_t out_size) const;
};

// Upper bound of the size of a sequence that FormatAnsiSequence() writes,
// including the null character. The longest sequence combines all text styles
// with an RGB foreground and an RGB background.
constexpr std::size_t kMaxAnsiSequenceSize = 48;

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
