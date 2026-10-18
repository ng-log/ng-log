// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// First rejects a plain floating-point number.

#include <ng-log/experimental/log.h>

int main() {
  nglog::experimental::Log(nglog::experimental::First{0.5}, "{}", 1);
}
