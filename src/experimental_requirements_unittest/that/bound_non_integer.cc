// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// BoundOf() is constrained to integer bounds.

#include <ng-log/experimental/ensure.h>
#include <ng-log/experimental/log.h>

int main() {
  const int array[3] = {};
  nglog::experimental::Ensure(nglog::experimental::That(1.0).BoundOf(array));
}
