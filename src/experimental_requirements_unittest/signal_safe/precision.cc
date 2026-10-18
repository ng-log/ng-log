// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// A signal-safe Log() must reject a precision above the limit for
// floating-point arguments, since it cannot be formatted without allocating.

#include <ng-log/experimental/signal_safe_log.h>

int main() {
  nglog::experimental::Log(nglog::experimental::SignalSafe, "{:.65f}", 1.0);
}
