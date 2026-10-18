// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Every rejects a bool, which is no count of occurrences.

#include <ng-log/experimental/log.h>

int main() {
  nglog::experimental::Log(nglog::experimental::Every{true}, "{}", 1);
}
