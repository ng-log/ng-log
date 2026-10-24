// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause

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

// NDEBUG usage helpers related to (RAW_)DCHECK:
//
// DEBUG_MODE is for small !NDEBUG uses like
//   if (DEBUG_MODE) foo.CheckThatFoo();
// instead of substantially more verbose
//   #ifndef NDEBUG
//     foo.CheckThatFoo();
//   #endif
//
#ifdef NDEBUG
enum { DEBUG_MODE = 0 };
#else
enum { DEBUG_MODE = 1 };
#endif

// The severity of DFATAL messages: FATAL in debug mode, ERROR otherwise.
// ng-log/logging.h defines DFATAL_LEVEL as it.
#ifdef NDEBUG
#  define NGLOG_DFATAL_LEVEL NGLOG_ERROR
#else
#  define NGLOG_DFATAL_LEVEL NGLOG_FATAL
#endif

// Expands to its argument in debug mode only. ng-log/logging.h defines
// IF_DEBUG_MODE as it.
#ifdef NDEBUG
#  define NGLOG_IF_DEBUG_MODE(x)
#else
#  define NGLOG_IF_DEBUG_MODE(x) x
#endif

}  // namespace nglog

// The severities are inline variables where available, whose external linkage
// lets the experimental C++ module expose them.
NGLOG_INLINE_VARIABLE
constexpr nglog::LogSeverity NGLOG_INFO =
    static_cast<nglog::LogSeverity>(nglog::internal::kInfoSeverity);
NGLOG_INLINE_VARIABLE
constexpr nglog::LogSeverity NGLOG_WARNING =
    static_cast<nglog::LogSeverity>(nglog::internal::kWarningSeverity);
NGLOG_INLINE_VARIABLE
constexpr nglog::LogSeverity NGLOG_ERROR =
    static_cast<nglog::LogSeverity>(nglog::internal::kErrorSeverity);
NGLOG_INLINE_VARIABLE
constexpr nglog::LogSeverity NGLOG_FATAL =
    static_cast<nglog::LogSeverity>(nglog::internal::kFatalSeverity);

#endif  // NGLOG_LOG_SEVERITY_H
