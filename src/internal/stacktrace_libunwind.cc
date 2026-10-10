// SPDX-FileCopyrightText: 2005-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Arun Sharma
//
// Produce a stack trace using libunwind.

#include "utilities.h"

extern "C" {
#define UNW_LOCAL_ONLY
#include <libunwind.h>
}
#include "ng-log/raw_logging.h"
#include "stacktrace.h"

namespace nglog {
inline namespace tools {

// Sometimes, we can try to get a stack trace from within a stack
// trace, because libunwind can call mmap (maybe indirectly via an
// internal mmap based memory allocator), and that mmap gets trapped
// and causes a stack-trace request.  If were to try to honor that
// recursive request, we'd end up with infinite recursion or deadlock.
// Luckily, it's safe to ignore those subsequent traces.  In such
// cases, we return 0 to indicate the situation.
// We can use the GCC __thread syntax here since libunwind is not supported on
// Windows.
static __thread bool g_tl_entered;  // Initialized to false.

// If you change this function, also change GetStackFrames below.
int GetStackTrace(void** result, int max_depth, int skip_count) {
  void* ip;
  int n = 0;
  unw_cursor_t cursor;
  unw_context_t uc;

  if (g_tl_entered) {
    return 0;
  }
  g_tl_entered = true;

  unw_getcontext(&uc);
  RAW_CHECK(unw_init_local(&cursor, &uc) >= 0, "unw_init_local failed");
  skip_count++;  // Do not include the "GetStackTrace" frame

  while (n < max_depth) {
    int ret =
        unw_get_reg(&cursor, UNW_REG_IP, reinterpret_cast<unw_word_t*>(&ip));
    if (ret < 0) {
      break;
    }
    if (skip_count > 0) {
      skip_count--;
    } else {
      result[n++] = ip;
    }
    ret = unw_step(&cursor);
    if (ret <= 0) {
      break;
    }
  }

  g_tl_entered = false;
  return n;
}

}  // namespace tools
}  // namespace nglog
