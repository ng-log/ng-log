// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_TEXT_STYLE_H
#define NGLOG_INTERNAL_TEXT_STYLE_H

#include <type_traits>

namespace nglog {
namespace internal {

enum class TextStyle : unsigned {
  kNone = 0,
  kBold = 1U << 0,
  kDim = 1U << 1,
  kItalic = 1U << 2,
  kUnderline = 1U << 3,
};

constexpr TextStyle operator|(TextStyle lhs, TextStyle rhs) noexcept {
  using UnderlyingType = std::underlying_type_t<TextStyle>;
  return static_cast<TextStyle>(static_cast<UnderlyingType>(lhs) |
                                static_cast<UnderlyingType>(rhs));
}

constexpr TextStyle operator&(TextStyle lhs, TextStyle rhs) noexcept {
  using UnderlyingType = std::underlying_type_t<TextStyle>;
  return static_cast<TextStyle>(static_cast<UnderlyingType>(lhs) &
                                static_cast<UnderlyingType>(rhs));
}

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_TEXT_STYLE_H
