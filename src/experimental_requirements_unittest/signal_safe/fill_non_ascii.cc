// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// A signal-safe Log() must reject a fill character that is not ASCII for a
// floating-point argument, since its padding would not fit the buffer.

#include <ng-log/experimental/signal_safe_log.h>

int main() {
  nglog::experimental::Log(nglog::experimental::SignalSafe, "{:é<5}", 1.0);
}
