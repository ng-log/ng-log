// SPDX-FileCopyrightText: 2000-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Portable implementation using glibc.
//
// Note:  The glibc implementation may cause a call to malloc.
// This can cause a deadlock in HeapProfiler.
#include <execinfo.h>

#include <cstring>

#include "stacktrace.h"

namespace nglog {
inline namespace tools {

// If you change this function, also change GetStackFrames below.
int GetStackTrace(void** result, int max_depth, int skip_count) {
  static const int kStackLength = 64;
  void* stack[kStackLength];
  int size;

  size = backtrace(stack, kStackLength);
  skip_count++;  // we want to skip the current frame as well
  int result_count = size - skip_count;
  if (result_count < 0) {
    result_count = 0;
  }
  if (result_count > max_depth) {
    result_count = max_depth;
  }
  for (int i = 0; i < result_count; i++) {
    result[i] = stack[i + skip_count];
  }

  return result_count;
}

}  // namespace tools
}  // namespace nglog
