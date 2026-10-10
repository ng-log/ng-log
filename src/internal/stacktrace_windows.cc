// SPDX-FileCopyrightText: 2000-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Andrew Schwartzmeyer
//
// Windows implementation using CaptureStackBackTrace.

// clang-format off
#include <windows.h> // Must come before <dbghelp.h>
#include <dbghelp.h>
// clang-format on

namespace nglog {
inline namespace tools {

int GetStackTrace(void** result, int max_depth, int skip_count) {
  if (max_depth > 64) {
    max_depth = 64;
  }
  skip_count++;  // we want to skip the current frame as well
  // This API is thread-safe (moreover it walks only the current thread).
  return CaptureStackBackTrace(static_cast<DWORD>(skip_count),
                               static_cast<DWORD>(max_depth), result, nullptr);
}

}  // namespace tools
}  // namespace nglog
