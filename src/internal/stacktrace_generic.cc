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

#include "ng-log/platform.h"
#include "stacktrace.h"

namespace nglog {
inline namespace tools {

namespace {

// glibc loads its unwinder on the first backtrace() call, which allocates
// memory and takes locks of the dynamic loader. Doing that during static
// initialization keeps it out of the failure signal handler, which may run
// after a crash inside the allocator.
struct BacktraceWarmUp {
  BacktraceWarmUp() {
    void* frame;
    backtrace(&frame, 1);
  }
};

const BacktraceWarmUp backtrace_warm_up;

// Returns the index of the first frame that belongs to the caller of
// GetStackTrace. Sanitizers intercept backtrace() and contribute frames of
// their own, so the frame is located by the return address into the caller
// instead of assuming that backtrace() reports this function first.
int CallerFrameIndex(void* const* stack, int size, const void* return_address) {
  for (int i = 0; i < size; ++i) {
    if (stack[i] == return_address) {
      return i;
    }
  }
  // Skip this function as well if the return address is unknown.
  return 1;
}

}  // namespace

// If you change this function, also change GetStackFrames below.
NGLOG_ATTRIBUTE_NOINLINE
int GetStackTrace(void** result, int max_depth, int skip_count) {
  static const int kStackLength = 64;
  void* stack[kStackLength];
  int size;

  size = backtrace(stack, kStackLength);
#if defined(__GNUC__)
  skip_count += CallerFrameIndex(stack, size, __builtin_return_address(0));
#else
  skip_count += CallerFrameIndex(stack, size, nullptr);
#endif
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
