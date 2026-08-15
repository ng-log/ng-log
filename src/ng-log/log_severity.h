// Copyright (c) 2024, Google Inc.
// Copyright (c) 2026, The ng-log contributors
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

#ifndef NGLOG_LOG_SEVERITY_H
#define NGLOG_LOG_SEVERITY_H

#include "ng-log/export.h"
#include "ng-log/platform.h"

namespace nglog {

namespace internal {

constexpr int kInfoSeverity = 0;
constexpr int kWarningSeverity = 1;
constexpr int kErrorSeverity = 2;
constexpr int kFatalSeverity = 3;

}  // namespace internal

// The recommended semantics of the log levels are as follows:
//
// INFO:
//   Use for state changes or other major events, or to aid debugging.
// WARNING:
//   Use for undesired but relatively expected events, which may indicate a
//   problem
// ERROR:
//   Use for undesired and unexpected events that the program can recover from.
//   All ERRORs should be actionable - it should be appropriate to file a bug
//   whenever an ERROR occurs in production.
// FATAL:
//   Use for undesired and unexpected events that the program cannot recover
//   from.

// Variables of type LogSeverity are widely taken to lie in the range
// [0, NUM_SEVERITIES-1].  Be careful to preserve this assumption if
// you ever need to change their values or add a new severity.

enum LogSeverity : int {
#ifndef NGLOG_NO_ABBREVIATED_SEVERITIES
#  ifdef ERROR
#  error "ERROR macro is defined. Define NGLOG_NO_ABBREVIATED_SEVERITIES before including logging.h. See https://ng-log.github.io/ng-log/stable/windows/ for details."
#  endif
  INFO = internal::kInfoSeverity,
  WARNING = internal::kWarningSeverity,
  ERROR = internal::kErrorSeverity,
  FATAL = internal::kFatalSeverity
#endif
};

NGLOG_INLINE_VARIABLE
constexpr int NUM_SEVERITIES = 4;

// DFATAL is FATAL in debug mode, ERROR in normal mode
#ifdef NDEBUG
#  define DFATAL_LEVEL NGLOG_ERROR
#else
#  define DFATAL_LEVEL NGLOG_FATAL
#endif

// NDEBUG usage helpers related to (RAW_)DCHECK:
//
// DEBUG_MODE is for small !NDEBUG uses like
//   if (DEBUG_MODE) foo.CheckThatFoo();
// instead of substantially more verbose
//   #ifndef NDEBUG
//     foo.CheckThatFoo();
//   #endif
//
// IF_DEBUG_MODE is for small !NDEBUG uses like
//   IF_DEBUG_MODE( string error; )
//   DCHECK(Foo(&error)) << error;
// instead of substantially more verbose
//   #ifndef NDEBUG
//     string error;
//     DCHECK(Foo(&error)) << error;
//   #endif
//
#ifdef NDEBUG
enum { DEBUG_MODE = 0 };
#  define IF_DEBUG_MODE(x)
#else
enum { DEBUG_MODE = 1 };
#  define IF_DEBUG_MODE(x) x
#endif

}  // namespace nglog

constexpr nglog::LogSeverity NGLOG_INFO =
    static_cast<nglog::LogSeverity>(nglog::internal::kInfoSeverity);
constexpr nglog::LogSeverity NGLOG_WARNING =
    static_cast<nglog::LogSeverity>(nglog::internal::kWarningSeverity);
constexpr nglog::LogSeverity NGLOG_ERROR =
    static_cast<nglog::LogSeverity>(nglog::internal::kErrorSeverity);
constexpr nglog::LogSeverity NGLOG_FATAL =
    static_cast<nglog::LogSeverity>(nglog::internal::kFatalSeverity);

#endif  // NGLOG_LOG_SEVERITY_H
