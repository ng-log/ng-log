// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Every rejects a plain floating-point number, which has no unit.

#include <ng-log/experimental/log.h>

int main() {
  nglog::experimental::Log(nglog::experimental::Every{0.5}, "{}", 1);
}
