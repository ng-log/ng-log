// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// IndexOf() is constrained to integer indices.

#include <ng-log/experimental/ensure.h>
#include <ng-log/experimental/log.h>

int main() {
  const int array[3] = {};
  nglog::experimental::Ensure(nglog::experimental::That(1.0).IndexOf(array));
}
