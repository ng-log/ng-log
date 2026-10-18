// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// A null check that fails during constant evaluation must fail to compile with
// a diagnostic naming the reason.

#include <ng-log/experimental/ensure.h>
#include <ng-log/experimental/log.h>

constexpr int Dereference(const int* p) {
  return *nglog::experimental::Ensure(nglog::experimental::NotNull(p, "p"));
}

constexpr int kValue = Dereference(nullptr);

int main() { return kValue; }
