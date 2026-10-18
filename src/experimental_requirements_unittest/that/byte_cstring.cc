// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Byte buffers must not compare by address while printing as text.

#include <ng-log/experimental/ensure.h>
#include <ng-log/experimental/log.h>

int main() {
  const unsigned char first[] = {1, 2, 3};
  const unsigned char second[] = {1, 2, 3};
  nglog::experimental::Ensure(nglog::experimental::That(first) == second);
}
