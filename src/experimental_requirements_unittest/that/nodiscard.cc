// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// An operand of That() that is never compared must be diagnosed.

#include <ng-log/experimental/ensure.h>

int main() {
  const bool ready = false;
  nglog::experimental::That(ready);
}
