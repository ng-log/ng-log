// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// First counts occurrences and rejects a period.

#include <ng-log/experimental/log.h>

#include <chrono>

int main() {
  nglog::experimental::Log(nglog::experimental::First{std::chrono::seconds{1}},
                           "{}", 1);
}
