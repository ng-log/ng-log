// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include <ng-log/logging.h>

int main() {
  LOG(INFO) << "info";
  LOG(WARNING) << "warning";
  LOG(ERROR) << "error";
  LOG(FATAL) << "fatal";
}
