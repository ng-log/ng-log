// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef NGLOG_TYPES_H
#define NGLOG_TYPES_H

#include <cstddef>
#include <cstdint>

#include "ng-log/platform.h"

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

#if defined(NGLOG_SANITIZE_THREAD)
// We need to identify the static variables as "benign" races
// to avoid noisy reports from TSAN.
extern "C" void AnnotateBenignRaceSized(const char* file, int line,
                                        const volatile void* mem, size_t size,
                                        const char* description);
#endif  // defined(NGLOG_SANITIZE_THREAD)

#endif  // NGLOG_TYPES_H
