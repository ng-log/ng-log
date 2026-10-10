// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include <ng-log/raw_logging.h>

int main() {
  RAW_LOG(INFO, "info");
  RAW_LOG(WARNING, "warning");
  RAW_LOG(ERROR, "error");
  RAW_LOG(FATAL, "fatal");
}
