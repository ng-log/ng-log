// Copyright (c) 2000 - 2007, Google Inc.
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
// Produce stack trace

#include <cstdint>  // for uintptr_t

#include "utilities.h"  // for OS_* macros

#if !defined(NGLOG_OS_WINDOWS)
#  include <sys/mman.h>
#  include <unistd.h>
#endif

#include <cstdio>  // for nullptr

#include "stacktrace.h"

namespace nglog {
inline namespace tools {

// Given a pointer to a stack frame, locate and return the calling
// stackframe, or return nullptr if no stackframe can be found. Perform sanity
// checks (the strictness of which is controlled by the boolean parameter
// "STRICT_UNWINDING") to reduce the chance that a bad pointer is returned.
template <bool STRICT_UNWINDING>
static void** NextStackFrame(void** old_sp) {
  void** new_sp = static_cast<void**>(*old_sp);

  // Check that the transition from frame pointer old_sp to frame
  // pointer new_sp isn't clearly bogus
  if (STRICT_UNWINDING) {
    // With the stack growing downwards, older stack frame must be
    // at a greater address that the current one.
    if (new_sp <= old_sp) return nullptr;
    // Assume stack frames larger than 100,000 bytes are bogus.
    if (reinterpret_cast<uintptr_t>(new_sp) -
            reinterpret_cast<uintptr_t>(old_sp) >
        100000) {
      return nullptr;
    }
  } else {
    // In the non-strict mode, allow discontiguous stack frames.
    // (alternate-signal-stacks for example).
    if (new_sp == old_sp) return nullptr;
    // And allow frames upto about 1MB.
    if ((new_sp > old_sp) && (reinterpret_cast<uintptr_t>(new_sp) -
                                  reinterpret_cast<uintptr_t>(old_sp) >
                              1000000)) {
      return nullptr;
    }
  }
  if (reinterpret_cast<uintptr_t>(new_sp) & (sizeof(void*) - 1)) {
    return nullptr;
  }
#ifdef __i386__
  // On 64-bit machines, the stack pointer can be very close to
  // 0xffffffff, so we explicitly check for a pointer into the
  // last two pages in the address space
  if ((uintptr_t)new_sp >= 0xffffe000) return nullptr;
#endif
#if !defined(NGLOG_OS_WINDOWS)
  if (!STRICT_UNWINDING) {
    // Lax sanity checks cause a crash in 32-bit tcmalloc/crash_reason_test
    // on AMD-based machines with VDSO-enabled kernels.
    // Make an extra sanity check to insure new_sp is readable.
    // Note: NextStackFrame<false>() is only called while the program
    //       is already on its last leg, so it's ok to be slow here.
    static int page_size = getpagesize();
    void* new_sp_aligned =
        reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(new_sp) &
                                static_cast<uintptr_t>(~(page_size - 1)));
    if (msync(new_sp_aligned, static_cast<size_t>(page_size), MS_ASYNC) == -1) {
      return nullptr;
    }
  }
#endif
  return new_sp;
}

// If you change this function, also change GetStackFrames below.
int GetStackTrace(void** result, int max_depth, int skip_count) {
  void** sp;

#ifdef __GNUC__
#  if __GNUC__ * 100 + __GNUC_MINOR__ >= 402
#    define USE_BUILTIN_FRAME_ADDRESS
#  endif
#endif

#ifdef USE_BUILTIN_FRAME_ADDRESS
  sp = reinterpret_cast<void**>(__builtin_frame_address(0));
#elif defined(__i386__)
  // Stack frame format:
  //    sp[0]   pointer to previous frame
  //    sp[1]   caller address
  //    sp[2]   first argument
  //    ...
  sp = (void**)&result - 2;
#elif defined(__x86_64__)
  // __builtin_frame_address(0) can return the wrong address on gcc-4.1.0-k8
  unsigned long rbp;
  // Move the value of the register %rbp into the local variable rbp.
  // We need 'volatile' to prevent this instruction from getting moved
  // around during optimization to before function prologue is done.
  // An alternative way to achieve this
  // would be (before this __asm__ instruction) to call Noop() defined as
  //   static void Noop() __attribute__ ((noinline));  // prevent inlining
  //   static void Noop() { asm(""); }  // prevent optimizing-away
  __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));
  // Arguments are passed in registers on x86-64, so we can't just
  // offset from &result
  sp = (void**)rbp;
#endif

  int n = 0;
  while (sp && n < max_depth) {
    if (*(sp + 1) == nullptr) {
      // In 64-bit code, we often see a frame that
      // points to itself and has a return address of 0.
      break;
    }
    if (skip_count > 0) {
      skip_count--;
    } else {
      result[n++] = *(sp + 1);
    }
    // Use strict unwinding rules.
    sp = NextStackFrame<true>(sp);
  }
  return n;
}
}  // namespace tools
}  // namespace nglog
