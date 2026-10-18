// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// A comparison that is not passed to Ensure() must be diagnosed.

#include <ng-log/experimental/ensure.h>

int main() {
  const int a = 1;
  // A comparison operator would also warn about an unused comparison, which
  // some compilers report first.
  nglog::experimental::Eq(a, 2, "a == 2");
}
