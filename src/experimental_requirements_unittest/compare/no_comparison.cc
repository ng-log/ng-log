// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// NGLOG_COMPARE() must reject an expression without a top-level comparison.

#include <ng-log/experimental/ensure.h>
#include <ng-log/experimental/log.h>

int main() {
  const bool ready = false;
  nglog::experimental::Ensure(NGLOG_COMPARE(ready), "");
}
