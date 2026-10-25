// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// A signal-safe check must reject Async like a signal-safe message, since
// passing its failure to the logging thread allocates and acquires a lock.

#include <ng-log/experimental/ensure.h>
#include <ng-log/experimental/signal_safe_log.h>

namespace ngx = nglog::experimental;

int main(int argc, char** /*argv*/) {
  ngx::Ensure(argc > 0, ngx::SignalSafe | ngx::Async, "message");
}
