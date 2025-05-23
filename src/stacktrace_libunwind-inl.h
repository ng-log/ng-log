// Copyright (c) 2005 - 2007, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Author: Arun Sharma
//
// Produce stack trace using libunwind

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
