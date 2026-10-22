// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include <ng-log/logging.h>

namespace {

// Occasional log statements form the body of an if statement without braces
// and without provoking warnings about an ambiguous else.
void LogOccasionallyWithoutBraces(bool condition) {
  if (condition) LOG_EVERY_N(INFO, 2) << "every n";
  if (condition) LOG_IF_EVERY_N(INFO, condition, 2) << "if every n";
  if (condition) LOG_FIRST_N(INFO, 2) << "first n";
  if (condition) LOG_EVERY_T(INFO, 1) << "every t";
  if (condition) PLOG_EVERY_N(INFO, 2) << "plog every n";
  if (condition) SYSLOG_EVERY_N(INFO, 2) << "syslog every n";
}

}  // namespace

int main() {
  LogOccasionallyWithoutBraces(true);
  LOG(INFO) << "info";
  LOG(WARNING) << "warning";
  LOG(ERROR) << "error";
  LOG(FATAL) << "fatal";
}
