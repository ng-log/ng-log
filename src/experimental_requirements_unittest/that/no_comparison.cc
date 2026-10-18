// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// That() must be compared before it is passed to Ensure().

#include <ng-log/experimental/ensure.h>
#include <ng-log/experimental/log.h>

int main() {
  const bool ready = false;
  nglog::experimental::Ensure(nglog::experimental::That(ready), "");
}
