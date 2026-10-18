// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// A check that fails during constant evaluation must fail to compile with a
// diagnostic naming the reason.

#include <ng-log/experimental/ensure.h>
#include <ng-log/experimental/log.h>

constexpr int Divide(int a, int b) {
  nglog::experimental::Ensure(b != 0, "b != 0");
  return a / b;
}

constexpr int kQuotient = Divide(1, 0);

int main() { return kQuotient; }
