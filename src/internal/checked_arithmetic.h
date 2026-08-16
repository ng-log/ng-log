// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_CHECKED_ARITHMETIC_H_
#define NGLOG_INTERNAL_CHECKED_ARITHMETIC_H_

#include <limits>
#include <type_traits>

namespace nglog {
namespace internal {

// Returns whether adding left and right cannot be represented by T.
template <typename T>
constexpr auto DoesAdditionOverflow(const T left, const T right)
    -> std::enable_if_t<
        std::is_integral<T>::value && std::is_unsigned<T>::value, bool> {
  return right > std::numeric_limits<T>::max() - left;
}

// Returns whether adding left and right cannot be represented by T.
template <typename T>
constexpr auto DoesAdditionOverflow(const T left, const T right)
    -> std::enable_if_t<std::is_integral<T>::value && std::is_signed<T>::value,
                        bool> {
  return (right > 0 && left > std::numeric_limits<T>::max() - right) ||
         (right < 0 && left < std::numeric_limits<T>::min() - right);
}

// Adds left and right to result when their sum is representable by T.
template <typename T>
constexpr auto CheckedAdd(const T left, const T right, T& result)
    -> std::enable_if_t<std::is_integral<T>::value, bool> {
  if (DoesAdditionOverflow(left, right)) {
    return false;
  }
  result = left + right;
  return true;
}

// Multiplies left and right if their product fits in T.
template <typename T>
constexpr bool CheckedMultiply(const T left, const T right, T& result) {
  if (left != 0 && right > std::numeric_limits<T>::max() / left) {
    return false;
  }
  result = left * right;
  return true;
}

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_CHECKED_ARITHMETIC_H_
