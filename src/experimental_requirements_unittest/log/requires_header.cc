// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// The regular destinations are only available with ng-log/experimental/log.h.

#include <ng-log/experimental/signal_safe_log.h>

int main() {
  nglog::experimental::Log(nglog::experimental::Warning, "regular");
}
