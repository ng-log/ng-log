// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Deferring a lazy argument to the logging thread must be rejected, since its
// callable may refer to state of the caller that ends with the call.

#include <ng-log/experimental/log.h>

namespace ngx = nglog::experimental;

int main() {
  int value = 0;
  ngx::Log(ngx::Async, "{}", ngx::Lazy([&value] { return value; }));
}
