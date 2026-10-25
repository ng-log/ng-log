// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// A signal-safe Log() must reject a wide fill character outside ASCII for
// floating-point arguments, since padding must fit a single byte, even U+012A,
// whose low byte is the ASCII character *.

#include <ng-log/experimental/signal_safe_log.h>

int main() {
  nglog::experimental::Log(nglog::experimental::SignalSafe, L"{:\x012A<5}",
                           1.0);
}
