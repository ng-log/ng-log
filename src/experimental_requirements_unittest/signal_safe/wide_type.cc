// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// A signal-safe Log() must reject a wide presentation type outside ASCII for
// floating-point arguments, even U+0161, whose low byte is the supported type
// a.

#include <ng-log/experimental/signal_safe_log.h>

int main() {
  nglog::experimental::Log(nglog::experimental::SignalSafe, L"{:.3\x0161}",
                           1.0);
}
