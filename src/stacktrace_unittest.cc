// Copyright (c) 2004, Google Inc.
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

#include "stacktrace.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>

#include "base/commandlineflags.h"
#include "config.h"
#include "ng-log/logging.h"
#include "utilities.h"

#ifdef HAVE_EXECINFO_BACKTRACE_SYMBOLS
#  include <execinfo.h>
#endif

#ifdef HAVE_STACKTRACE

// Obtain a backtrace, verify that the expected callers are present in the
// backtrace, and maybe print the backtrace to stdout.

// The sequence of functions whose return addresses we expect to see in the
// backtrace.
const int BACKTRACE_STEPS = 6;

struct AddressRange {
  const void *start, *end;
};

// Expected function [start,end] range.
AddressRange expected_range[BACKTRACE_STEPS];

#  if __GNUC__
// Using GCC extension: address of a label can be taken with '&&label'.
// Start should be a label somewhere before recursive call, end somewhere
// after it.
#    define INIT_ADDRESS_RANGE(fn, start_label, end_label, prange) \
      do {                                                         \
        (prange)->start = &&start_label;                           \
        (prange)->end = &&end_label;                               \
        CHECK_LT((prange)->start, (prange)->end);                  \
      } while (0)
// This macro expands into "unmovable" code (opaque to GCC), and that
// prevents GCC from moving a_label up or down in the code.
// Without it, there is no code following the 'end' label, and GCC
// (4.3.1, 4.4.0) thinks it safe to assign &&end an address that is before
// the recursive call.
#    define DECLARE_ADDRESS_LABEL(a_label) \
    a_label:                               \
      do {                                 \
        __asm__ __volatile__("");          \
      } while (0)
// Gcc 4.4.0 may split function into multiple chunks, and the chunk
// performing recursive call may end up later in the code then the return
// instruction (this actually happens with FDO).
// Adjust function range from __builtin_return_address.
#    define ADJUST_ADDRESS_RANGE_FROM_RA(prange)                             \
      do {                                                                   \
        void* ra = __builtin_return_address(0);                              \
        CHECK_LT((prange)->start, ra);                                       \
        if (ra > (prange)->end) {                                            \
          printf("Adjusting range from %p..%p to %p..%p\n", (prange)->start, \
                 (prange)->end, (prange)->start, ra);                        \
          (prange)->end = ra;                                                \
        }                                                                    \
      } while (0)
#  else
// Assume the Check* functions below are not longer than 256 bytes.
#    define INIT_ADDRESS_RANGE(fn, start_label, end_label, prange) \
      do {                                                         \
        (prange)->start = reinterpret_cast<const void*>(&fn);      \
        (prange)->end = reinterpret_cast<const char*>(&fn) + 256;  \
      } while (0)
#    define DECLARE_ADDRESS_LABEL(a_label) \
      do {                                 \
      } while (0)
#    define ADJUST_ADDRESS_RANGE_FROM_RA(prange) \
      do {                                       \
      } while (0)
#  endif  // __GNUC__

//-----------------------------------------------------------------------//

static void CheckRetAddrIsInFunction(void* ret_addr,
                                     const AddressRange& range) {
  CHECK_GE(ret_addr, range.start);
  CHECK_LE(ret_addr, range.end);
}

//-----------------------------------------------------------------------//

#  if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wgnu-label-as-value"
#  endif

void ATTRIBUTE_NOINLINE CheckStackTrace(int);
static void ATTRIBUTE_NOINLINE CheckStackTraceLeaf() {
  const int STACK_LEN = 10;
  void* stack[STACK_LEN];
  int size;

  ADJUST_ADDRESS_RANGE_FROM_RA(&expected_range[1]);
  INIT_ADDRESS_RANGE(CheckStackTraceLeaf, start, end, &expected_range[0]);
  DECLARE_ADDRESS_LABEL(start);
  size = nglog::GetStackTrace(stack, STACK_LEN, 0);
  printf("Obtained %d stack frames.\n", size);
  CHECK_GE(size, 1);
  CHECK_LE(size, STACK_LEN);

  if (true) {
#  ifdef HAVE_EXECINFO_BACKTRACE_SYMBOLS
    char** strings = backtrace_symbols(stack, size);
    printf("Obtained %d stack frames.\n", size);
    for (int i = 0; i < size; i++) {
      printf("%s %p\n", strings[i], stack[i]);
    }

    union {
      void (*p1)(int);
      void* p2;
    } p = {&CheckStackTrace};

    printf("CheckStackTrace() addr: %p\n", p.p2);
    free(strings);
#  endif
  }
  for (int i = 0; i < BACKTRACE_STEPS; i++) {
    printf("Backtrace %d: expected: %p..%p  actual: %p ... ", i,
           expected_range[i].start, expected_range[i].end, stack[i]);
    fflush(stdout);
    CheckRetAddrIsInFunction(stack[i], expected_range[i]);
    printf("OK\n");
  }
  DECLARE_ADDRESS_LABEL(end);
}

//-----------------------------------------------------------------------//

/* Dummy functions to make the backtrace more interesting. */
static void ATTRIBUTE_NOINLINE CheckStackTrace4(int i) {
  ADJUST_ADDRESS_RANGE_FROM_RA(&expected_range[2]);
  INIT_ADDRESS_RANGE(CheckStackTrace4, start, end, &expected_range[1]);
  DECLARE_ADDRESS_LABEL(start);
  for (int j = i; j >= 0; j--) {
    CheckStackTraceLeaf();
  }
  DECLARE_ADDRESS_LABEL(end);
}
static void ATTRIBUTE_NOINLINE CheckStackTrace3(int i) {
  ADJUST_ADDRESS_RANGE_FROM_RA(&expected_range[3]);
  INIT_ADDRESS_RANGE(CheckStackTrace3, start, end, &expected_range[2]);
  DECLARE_ADDRESS_LABEL(start);
  for (int j = i; j >= 0; j--) {
    CheckStackTrace4(j);
  }
  DECLARE_ADDRESS_LABEL(end);
}
static void ATTRIBUTE_NOINLINE CheckStackTrace2(int i) {
  ADJUST_ADDRESS_RANGE_FROM_RA(&expected_range[4]);
  INIT_ADDRESS_RANGE(CheckStackTrace2, start, end, &expected_range[3]);
  DECLARE_ADDRESS_LABEL(start);
  for (int j = i; j >= 0; j--) {
    CheckStackTrace3(j);
  }
  DECLARE_ADDRESS_LABEL(end);
}
static void ATTRIBUTE_NOINLINE CheckStackTrace1(int i) {
  ADJUST_ADDRESS_RANGE_FROM_RA(&expected_range[5]);
  INIT_ADDRESS_RANGE(CheckStackTrace1, start, end, &expected_range[4]);
  DECLARE_ADDRESS_LABEL(start);
  for (int j = i; j >= 0; j--) {
    CheckStackTrace2(j);
  }
  DECLARE_ADDRESS_LABEL(end);
}

#  ifndef __GNUC__
// On non-GNU environment, we use the address of `CheckStackTrace` to
// guess the address range of this function. This guess is wrong for
// non-static function on Windows. This is probably because
// `&CheckStackTrace` returns the address of a trampoline like PLT,
// not the actual address of `CheckStackTrace`.
// See https://github.com/google/glog/issues/421 for the detail.
static
#  endif
    void ATTRIBUTE_NOINLINE
    CheckStackTrace(int i) {
  INIT_ADDRESS_RANGE(CheckStackTrace, start, end, &expected_range[5]);
  DECLARE_ADDRESS_LABEL(start);
  for (int j = i; j >= 0; j--) {
    CheckStackTrace1(j);
  }
  DECLARE_ADDRESS_LABEL(end);
}

#  if defined(__clang__)
#    pragma clang diagnostic pop
#  endif

namespace {
void handle_abort(int /*code*/) { std::exit(EXIT_FAILURE); }
}  // namespace

//-----------------------------------------------------------------------//

int main(int, char** argv) {
#  if defined(_MSC_VER)
  // Avoid presenting an interactive dialog that will cause the test to time
  // out.
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#  endif  // defined(_MSC_VER)
  std::signal(SIGABRT, handle_abort);

  FLAGS_logtostderr = true;
  nglog::InitializeLogging(argv[0]);

  CheckStackTrace(0);

  printf("PASS\n");
  return 0;
}

#else
int main() {

#  ifdef NGLOG_BAZEL_BUILD
  printf("HAVE_STACKTRACE is expected to be defined in Bazel tests\n");
  exit(EXIT_FAILURE);
#  endif  // NGLOG_BAZEL_BUILD

  printf("PASS (no stacktrace support)\n");
  return 0;
}
#endif    // HAVE_STACKTRACE
