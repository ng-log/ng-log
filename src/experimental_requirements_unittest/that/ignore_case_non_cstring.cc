// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// IgnoreCase() is constrained to C strings.

#include <ng-log/experimental/ensure.h>

int main() {
  const int value = 1;
  // Discards the operand, so that only the constraint of IgnoreCase() can
  // reject the call.
  static_cast<void>(nglog::experimental::That(value).IgnoreCase());
}
