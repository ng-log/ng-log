// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// A message passed to NGLOG_COMPARE() instead of Ensure() must be rejected
// instead of discarding the comparison before it.

#include <ng-log/experimental/ensure.h>
#include <ng-log/experimental/log.h>

int main() {
  const int a = 1;
  const int b = 2;
  nglog::experimental::Ensure(NGLOG_COMPARE(a == b, "msg"));
}
