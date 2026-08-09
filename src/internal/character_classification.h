// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_CHARACTER_CLASSIFICATION_H_
#define NGLOG_INTERNAL_CHARACTER_CLASSIFICATION_H_

namespace nglog {
namespace internal {

constexpr bool IsLower(char c) noexcept { return c >= 'a' && c <= 'z'; }

constexpr bool IsUpper(char c) noexcept { return c >= 'A' && c <= 'Z'; }

constexpr bool IsAlpha(char c) noexcept { return IsLower(c) || IsUpper(c); }

constexpr bool IsDecimalDigit(char c) noexcept { return c >= '0' && c <= '9'; }

constexpr bool IsAlphanumeric(char c) noexcept {
  return IsAlpha(c) || IsDecimalDigit(c);
}

constexpr bool IsLowerHexDigit(char c) noexcept {
  return IsDecimalDigit(c) || (c >= 'a' && c <= 'f');
}

constexpr bool IsWhitespace(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' ||
         c == '\r';
}

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_CHARACTER_CLASSIFICATION_H_
