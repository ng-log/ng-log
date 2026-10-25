// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Passing a signal-safe message to the logging thread must be rejected, since
// queuing it allocates and acquires a lock.

#include <ng-log/experimental/signal_safe_log.h>

namespace ngx = nglog::experimental;

int main() { ngx::Log(ngx::SignalSafe | ngx::Async, "message"); }
