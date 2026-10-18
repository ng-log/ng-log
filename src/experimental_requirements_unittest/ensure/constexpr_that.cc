// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// A comparison of That() that fails during constant evaluation must fail to
// compile with a diagnostic naming the reason.

#include <ng-log/experimental/ensure.h>
#include <ng-log/experimental/log.h>

constexpr int Divide(int a, int b) {
  return a / nglog::experimental::Ensure(nglog::experimental::That(b) != 0);
}

constexpr int kQuotient = Divide(1, 0);

int main() { return kQuotient; }
