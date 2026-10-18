// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// An operand of NGLOG_COMPARE() without a comparison must be diagnosed.

#include <ng-log/experimental/ensure.h>

int main() {
  const int a = 1;
  NGLOG_COMPARE(a);
}
