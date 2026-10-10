// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include <ng-log/logging.h>

int main() {
  // Must not compile
  nglog::LogMessage{__FILE__, __LINE__, -1};
  // Cast to int to avoid implicit conversion to nullptr
  nglog::LogMessage{__FILE__, __LINE__, static_cast<int>(0)};
  nglog::LogMessage{__FILE__, __LINE__, 1};
  nglog::LogMessage{__FILE__, __LINE__, 2};
  nglog::LogMessage{__FILE__, __LINE__, 3};
}
