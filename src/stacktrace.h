// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Routines to extract the current stack trace.  These functions are
// thread-safe.

#ifndef NGLOG_INTERNAL_STACKTRACE_H
#define NGLOG_INTERNAL_STACKTRACE_H

#include "config.h"
#include "ng-log/export.h"

namespace nglog {
inline namespace tools {

// The most frames a failure stack dump prints, and the most a CrashReason
// records.
constexpr int kMaxStackDumpFrames = 32;

// The line that ends a stack dump cut off at kMaxStackDumpFrames, in GDB's
// wording for a backtrace cut off at its limit.
constexpr char kStackDumpCutOffMarker[] = "    (More stack frames follow...)\n";

#if defined(HAVE_STACKTRACE)

// Stores the return addresses of up to max_depth callers in result,
// skipping the innermost skip_count frames besides this function, and
// returns their number.
// Example:
//      main() { foo(); }
//      foo() { bar(); }
//      bar() {
//        void* result[10];
//        int depth = GetStackTrace(result, 10, 1);
//      }
//
// This produces:
//      result[0]       foo
//      result[1]       main
//           ....       ...
//
// "result" must not be nullptr.
NGLOG_NO_EXPORT int GetStackTrace(void** result, int max_depth, int skip_count);

#endif  // defined(HAVE_STACKTRACE)

}  // namespace tools
}  // namespace nglog

#endif  // NGLOG_INTERNAL_STACKTRACE_H
