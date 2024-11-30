
// Copyright (c) 2024, Google Inc.
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

#ifndef NGLOG_TYPES_H
#define NGLOG_TYPES_H

#include <cstddef>
#include <cstdint>

namespace nglog {

using int32 = std::int32_t;
using uint32 = std::uint32_t;
using int64 = std::int64_t;
using uint64 = std::uint64_t;

}  // namespace nglog

#if defined(__has_feature)
#  if __has_feature(thread_sanitizer)
#    define NGLOG_SANITIZE_THREAD 1
#  endif
#endif

#if !defined(NGLOG_SANITIZE_THREAD) && defined(__SANITIZE_THREAD__) && \
    __SANITIZE_THREAD__
#  define NGLOG_SANITIZE_THREAD 1
#endif

#if defined(NGLOG_SANITIZE_THREAD)
#  define NGLOG_IFDEF_THREAD_SANITIZER(X) X
#else
#  define NGLOG_IFDEF_THREAD_SANITIZER(X)
#endif

#if defined(_MSC_VER)
#  define NGLOG_MSVC_PUSH_DISABLE_WARNING(n) \
    __pragma(warning(push)) __pragma(warning(disable : n))
#  define NGLOG_MSVC_POP_WARNING() __pragma(warning(pop))
#else
#  define NGLOG_MSVC_PUSH_DISABLE_WARNING(n)
#  define NGLOG_MSVC_POP_WARNING()
#endif

#if defined(NGLOG_SANITIZE_THREAD)
// We need to identify the static variables as "benign" races
// to avoid noisy reports from TSAN.
extern "C" void AnnotateBenignRaceSized(const char* file, int line,
                                        const volatile void* mem, size_t size,
                                        const char* description);
#endif  // defined(NGLOG_SANITIZE_THREAD)

#endif  // NGLOG_TYPES_H
