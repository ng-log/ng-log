// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// The prefixed forms of DFATAL_LEVEL and IF_DEBUG_MODE are available without
// the unprefixed macros of ng-log/logging.h.

#include <ng-log/log_severity.h>

int main() {
  constexpr nglog::LogSeverity kSeverity = NGLOG_DFATAL_LEVEL;
  NGLOG_IF_DEBUG_MODE(static_cast<void>(kSeverity);)
  return kSeverity == NGLOG_INFO ? 1 : 0;
}
