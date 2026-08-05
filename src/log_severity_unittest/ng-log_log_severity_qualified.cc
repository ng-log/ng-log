// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause

#include <ng-log/log_severity.h>

int main() {
  nglog::LogSeverity severity = nglog::NGLOG_INFO;
  return static_cast<int>(severity);
}
