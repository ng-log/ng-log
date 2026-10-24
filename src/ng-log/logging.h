// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Ray Sidney
//
// This file contains #include information about logging-related stuff.
// Pretty much everybody needs to #include this file so that they can
// log various happenings.
//
#ifndef NGLOG_LOGGING_H
#define NGLOG_LOGGING_H

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "ng-log/internal/styled_value.h"  // IWYU pragma: export
#include "ng-log/log_message.h"            // IWYU pragma: export
#include "ng-log/log_setup.h"              // IWYU pragma: export
#include "ng-log/log_severity.h"           // IWYU pragma: export
#include "ng-log/log_sink.h"               // IWYU pragma: export
#include "ng-log/platform.h"
#include "ng-log/types.h"
#include "ng-log/vlog_is_on.h"  // IWYU pragma: export

// Make a bunch of macros for logging.  The way to log things is to stream
// things to LOG(<a particular severity level>).  E.g.,
//
//   LOG(INFO) << "Found " << num_cookies << " cookies";
//
// You can capture log messages in a string, rather than reporting them
// immediately:
//
//   vector<string> errors;
//   LOG_STRING(ERROR, &errors) << "Couldn't parse cookie #" << cookie_num;
//
// This pushes back the new error onto 'errors'; if given a nullptr pointer,
// it reports the error via LOG(ERROR).
//
// You can also do conditional logging:
//
//   LOG_IF(INFO, num_cookies > 10) << "Got lots of cookies";
//
// You can also do occasional logging (log every n'th occurrence of an
// event):
//
//   LOG_EVERY_N(INFO, 10) << "Got the " << nglog::COUNTER << "th cookie";
//
// The above will cause log messages to be output on the 1st, 11th, 21st, ...
// times it is executed.  Note that the special nglog::COUNTER value is used
// to identify which repetition is happening.
//
// You can also do occasional conditional logging (log every n'th
// occurrence of an event, when condition is satisfied):
//
//   LOG_IF_EVERY_N(INFO, (size > 1024), 10) << "Got the " << nglog::COUNTER
//                                           << "th big cookie";
//
// You can log messages the first N times your code executes a line. E.g.
//
//   LOG_FIRST_N(INFO, 20) << "Got the " << nglog::COUNTER << "th cookie";
//
// Outputs log messages for the first 20 times it is executed.
//
// Analogous SYSLOG, SYSLOG_IF, and SYSLOG_EVERY_N macros are available.
// These log to syslog as well as to the normal logs.  If you use these at
// all, you need to be aware that syslog can drastically reduce performance,
// especially if it is configured for remote logging!  Don't use these
// unless you fully understand this and have a concrete need to use them.
// Even then, try to minimize your use of them.
//
// There are also "debug mode" logging macros like the ones above:
//
//   DLOG(INFO) << "Found cookies";
//
//   DLOG_IF(INFO, num_cookies > 10) << "Got lots of cookies";
//
//   DLOG_EVERY_N(INFO, 10) << "Got the " << nglog::COUNTER << "th cookie";
//
// All "debug mode" logging is compiled away to nothing for non-debug mode
// compiles.
//
// We also have
//
//   LOG_ASSERT(assertion);
//   DLOG_ASSERT(assertion);
//
// which is syntactic sugar for {,D}LOG_IF(FATAL, assert fails) << assertion;
//
// There are "verbose level" logging macros.  They look like
//
//   VLOG(1) << "I'm printed when you run the program with --v=1 or more";
//   VLOG(2) << "I'm printed when you run the program with --v=2 or more";
//
// These always log at the INFO log level (when they log at all).
// The verbose logging can also be turned on module-by-module.  For instance,
//    --vmodule=mapreduce=2,file=1,gfs*=3 --v=0
// will cause:
//   a. VLOG(2) and lower messages to be printed from mapreduce.{h,cc}
//   b. VLOG(1) and lower messages to be printed from file.{h,cc}
//   c. VLOG(3) and lower messages to be printed from files prefixed with "gfs"
//   d. VLOG(0) and lower messages to be printed from elsewhere
//
// The wildcarding functionality shown by (c) supports both '*' (match
// 0 or more characters) and '?' (match any single character) wildcards.
//
// There's also VLOG_IS_ON(n) "verbose level" condition macro. To be used as
//
//   if (VLOG_IS_ON(2)) {
//     // do some logging preparation and logging
//     // that can't be accomplished with just VLOG(2) << ...;
//   }
//
// There are also VLOG_IF, VLOG_EVERY_N and VLOG_IF_EVERY_N "verbose level"
// condition macros for sample cases, when some extra computation and
// preparation for logs is not needed.
//   VLOG_IF(1, (size > 1024))
//      << "I'm printed when size is more than 1024 and when you run the "
//         "program with --v=1 or more";
//   VLOG_EVERY_N(1, 10)
//      << "I'm printed every 10th occurrence, and when you run the program "
//         "with --v=1 or more. Present occurrence is " << nglog::COUNTER;
//   VLOG_IF_EVERY_N(1, (size > 1024), 10)
//      << "I'm printed on every 10th occurrence of case when size is more "
//         " than 1024, when you run the program with --v=1 or more. ";
//         "Present occurrence is " << nglog::COUNTER;
//
// The supported severity levels for macros that allow you to specify one
// are (in increasing order of severity) INFO, WARNING, ERROR, and FATAL.
// Note that messages of a given severity are logged not only in the
// logfile for that severity, but also in all logfiles of lower severity.
// E.g., a message of severity FATAL will be logged to the logfiles of
// severity FATAL, ERROR, WARNING, and INFO.
//
// There is also the special severity of DFATAL, which logs FATAL in
// debug mode, ERROR in normal mode.
//
// Very important: logging a message at the FATAL severity level causes
// the program to terminate (after the message is logged).
//
// Unless otherwise specified, logs will be written to the filename
// "<program name>.<hostname>.<user name>.log.<severity level>.", followed
// by the date, time, and pid (you can't prevent the date, time, and pid
// from being in the filename).
//
// The logging code takes two flags:
//     --v=#           set the verbose level
//     --logtostderr   log all the messages to stderr instead of to logfiles

// LOG LINE PREFIX FORMAT
//
// Log lines have this form:
//
//     Lyyyymmdd hh:mm:ss.uuuuuu threadid file:line] msg...
//
// where the fields are defined as follows:
//
//   L                A single character, representing the log level
//                    (eg 'I' for INFO)
//   yyyy             The year
//   mm               The month (zero padded; ie May is '05')
//   dd               The day (zero padded)
//   hh:mm:ss.uuuuuu  Time in hours, minutes and fractional seconds
//   threadid         The space-padded thread ID as returned by GetTID()
//                    (this matches the PID on Linux)
//   file             The file name
//   line             The line number
//   msg              The user-supplied message
//
// Example:
//
//   I1103 11:57:31.739339 24395 google.cc:2341] Command line: ./some_prog
//   I1103 11:57:31.739403 24395 google.cc:2342] Process id 24395
//
// NOTE: although the microseconds are useful for comparing events on
// a single machine, clocks on different machines may not be well
// synchronized.  Hence, use caution when comparing the low bits of
// timestamps from different machines.

// Log messages below the NGLOG_STRIP_LOG level will be compiled away for
// security reasons. See LOG(severity) below.

// A few definitions of macros that don't generate much code.  Since
// LOG(INFO) and its ilk are used all over our code, it's
// better to have compact code for these operations.

#if NGLOG_STRIP_LOG == 0
#  define NGLOG_COMPACT_LOG_INFO nglog::LogMessage(__FILE__, __LINE__)
#  define LOG_TO_STRING_INFO(message) \
    nglog::LogMessage(__FILE__, __LINE__, NGLOG_INFO, message)
#else
#  define NGLOG_COMPACT_LOG_INFO nglog::NullStream()
#  define LOG_TO_STRING_INFO(message) nglog::NullStream()
#endif

#if NGLOG_STRIP_LOG <= 1
#  define NGLOG_COMPACT_LOG_WARNING \
    nglog::LogMessage(__FILE__, __LINE__, NGLOG_WARNING)
#  define LOG_TO_STRING_WARNING(message) \
    nglog::LogMessage(__FILE__, __LINE__, NGLOG_WARNING, message)
#else
#  define NGLOG_COMPACT_LOG_WARNING nglog::NullStream()
#  define LOG_TO_STRING_WARNING(message) nglog::NullStream()
#endif

#if NGLOG_STRIP_LOG <= 2
#  define NGLOG_COMPACT_LOG_ERROR \
    nglog::LogMessage(__FILE__, __LINE__, NGLOG_ERROR)
#  define LOG_TO_STRING_ERROR(message) \
    nglog::LogMessage(__FILE__, __LINE__, NGLOG_ERROR, message)
#else
#  define NGLOG_COMPACT_LOG_ERROR nglog::NullStream()
#  define LOG_TO_STRING_ERROR(message) nglog::NullStream()
#endif

#if NGLOG_STRIP_LOG <= 3
#  define NGLOG_COMPACT_LOG_FATAL nglog::LogMessageFatal(__FILE__, __LINE__)
#  define LOG_TO_STRING_FATAL(message) \
    nglog::LogMessage(__FILE__, __LINE__, NGLOG_FATAL, message)
#else
#  define NGLOG_COMPACT_LOG_FATAL nglog::NullStreamFatal()
#  define LOG_TO_STRING_FATAL(message) nglog::NullStreamFatal()
#endif

#define DCHECK_IS_ON() NGLOG_DCHECK_IS_ON()

// DFATAL is FATAL in debug mode, ERROR in normal mode
#define DFATAL_LEVEL NGLOG_DFATAL_LEVEL

// IF_DEBUG_MODE is for small !NDEBUG uses like
//   IF_DEBUG_MODE( string error; )
//   DCHECK(Foo(&error)) << error;
// instead of substantially more verbose
//   #ifndef NDEBUG
//     string error;
//     DCHECK(Foo(&error)) << error;
//   #endif
//
#define IF_DEBUG_MODE(x) NGLOG_IF_DEBUG_MODE(x)

// For DFATAL, we want to use LogMessage (as opposed to
// LogMessageFatal), to be consistent with the original behavior.
#if !DCHECK_IS_ON()
#  define NGLOG_COMPACT_LOG_DFATAL NGLOG_COMPACT_LOG_ERROR
#elif NGLOG_STRIP_LOG <= 3
#  define NGLOG_COMPACT_LOG_DFATAL \
    nglog::LogMessage(__FILE__, __LINE__, NGLOG_FATAL)
#else
#  define NGLOG_COMPACT_LOG_DFATAL nglog::NullStreamFatal()
#endif

#define NGLOG_LOG_INFO(counter)                              \
  nglog::LogMessage(__FILE__, __LINE__, NGLOG_INFO, counter, \
                    &nglog::LogMessage::SendToLog)
#define SYSLOG_INFO(counter)                                 \
  nglog::LogMessage(__FILE__, __LINE__, NGLOG_INFO, counter, \
                    &nglog::LogMessage::SendToSyslogAndLog)
#define NGLOG_LOG_WARNING(counter)                              \
  nglog::LogMessage(__FILE__, __LINE__, NGLOG_WARNING, counter, \
                    &nglog::LogMessage::SendToLog)
#define SYSLOG_WARNING(counter)                                 \
  nglog::LogMessage(__FILE__, __LINE__, NGLOG_WARNING, counter, \
                    &nglog::LogMessage::SendToSyslogAndLog)
#define NGLOG_LOG_ERROR(counter)                              \
  nglog::LogMessage(__FILE__, __LINE__, NGLOG_ERROR, counter, \
                    &nglog::LogMessage::SendToLog)
#define SYSLOG_ERROR(counter)                                 \
  nglog::LogMessage(__FILE__, __LINE__, NGLOG_ERROR, counter, \
                    &nglog::LogMessage::SendToSyslogAndLog)
#define NGLOG_LOG_FATAL(counter)                              \
  nglog::LogMessage(__FILE__, __LINE__, NGLOG_FATAL, counter, \
                    &nglog::LogMessage::SendToLog)
#define SYSLOG_FATAL(counter)                                 \
  nglog::LogMessage(__FILE__, __LINE__, NGLOG_FATAL, counter, \
                    &nglog::LogMessage::SendToSyslogAndLog)
#define NGLOG_LOG_DFATAL(counter)                              \
  nglog::LogMessage(__FILE__, __LINE__, DFATAL_LEVEL, counter, \
                    &nglog::LogMessage::SendToLog)
#define SYSLOG_DFATAL(counter)                                 \
  nglog::LogMessage(__FILE__, __LINE__, DFATAL_LEVEL, counter, \
                    &nglog::LogMessage::SendToSyslogAndLog)

#if defined(NGLOG_OS_WINDOWS)
// A very useful logging macro to log windows errors:
#  define LOG_SYSRESULT(result)                                               \
    if (FAILED(HRESULT_FROM_WIN32(result))) {                                 \
      const std::string message = nglog::tools::FormatWindowsMessage(result); \
      if (!message.empty()) {                                                 \
        nglog::LogMessage(__FILE__, __LINE__, NGLOG_ERROR, 0,                 \
                          &nglog::LogMessage::SendToLog)                      \
                .stream()                                                     \
            << message;                                                       \
      }                                                                       \
    }
#endif

// We use the preprocessor's merging operator, "##", so that, e.g.,
// LOG(INFO) becomes the token NGLOG_LOG_INFO.  There's some funny
// subtle difference between ostream member streaming functions (e.g.,
// ostream::operator<<(int) and ostream non-member streaming functions
// (e.g., ::operator<<(ostream&, string&): it turns out that it's
// impossible to stream something like a string directly to an unnamed
// ostream. We employ a neat hack by calling the stream() member
// function of LogMessage which seems to avoid the problem.
#define NGLOG_LOG_SEVERITY_INFO NGLOG_INFO
#define NGLOG_LOG_SEVERITY_WARNING NGLOG_WARNING
#define NGLOG_LOG_SEVERITY_ERROR NGLOG_ERROR
#define NGLOG_LOG_SEVERITY_FATAL NGLOG_FATAL
#define NGLOG_LOG_SEVERITY_DFATAL DFATAL_LEVEL
#if defined(NGLOG_NO_ABBREVIATED_SEVERITIES)
#  define NGLOG_LOG_SEVERITY_0 NGLOG_ERROR
#endif
#define NGLOG_LOG_SEVERITY(severity) NGLOG_LOG_SEVERITY_##severity
// Comparing with NGLOG_FATAL in the macro lets compilers see that LOG(FATAL)
// does not return without inlining the function.
#define NGLOG_LOG_SEVERITY_ENABLED(severity)      \
  (NGLOG_LOG_SEVERITY(severity) == NGLOG_FATAL || \
   nglog::internal::IsLogSeverityEnabled(NGLOG_LOG_SEVERITY(severity)))

#define NGLOG_LOG_IF(severity, condition) \
  static_cast<void>(0),                   \
      !(condition) ? (void)0              \
                   : nglog::internal::LogMessageVoidify() & LOG(severity)

#define LOG(severity)                          \
  (NGLOG_LOG_SEVERITY_ENABLED(severity)        \
       ? NGLOG_COMPACT_LOG_##severity.stream() \
       : nglog::NullStream().stream())
#define SYSLOG(severity) SYSLOG_##severity(0).stream()

// If a non-nullptr sink pointer is given, we push this message to that sink.
// For LOG_TO_SINK we then do normal LOG(severity) logging as well.
// This is useful for capturing messages and passing/storing them
// somewhere more specific than the global log of the process.
// Argument types:
//   LogSink* sink;
//   LogSeverity severity;
// The cast is to disambiguate nullptr arguments.
#define LOG_TO_SINK(sink, severity)                           \
  nglog::LogMessage(__FILE__, __LINE__, NGLOG_##severity,     \
                    static_cast<nglog::LogSink*>(sink), true) \
      .stream()
#define LOG_TO_SINK_BUT_NOT_TO_LOGFILE(sink, severity)         \
  nglog::LogMessage(__FILE__, __LINE__, NGLOG_##severity,      \
                    static_cast<nglog::LogSink*>(sink), false) \
      .stream()

// If a non-nullptr string pointer is given, we write this message to that
// string. We then do normal LOG(severity) logging as well. This is useful for
// capturing messages and storing them somewhere more specific than the global
// log of the process. Argument types:
//   string* message;
//   LogSeverity severity;
// The cast is to disambiguate nullptr arguments.
// NOTE: LOG(severity) expands to LogMessage().stream() for the specified
// severity.
#define LOG_TO_STRING(severity, message) \
  LOG_TO_STRING_##severity(static_cast<std::string*>(message)).stream()

// If a non-nullptr pointer is given, we push the message onto the end
// of a vector of strings; otherwise, we report it with LOG(severity).
// This is handy for capturing messages and perhaps passing them back
// to the caller, rather than reporting them immediately.
// Argument types:
//   LogSeverity severity;
//   vector<string> *outvec;
// The cast is to disambiguate nullptr arguments.
#define LOG_STRING(severity, outvec)                                       \
  LOG_TO_STRING_##severity(static_cast<std::vector<std::string>*>(outvec)) \
      .stream()

#define LOG_IF(severity, condition) \
  NGLOG_LOG_IF(severity, (condition) && NGLOG_LOG_SEVERITY_ENABLED(severity))
#define SYSLOG_IF(severity, condition) \
  static_cast<void>(0),                \
      !(condition) ? (void)0           \
                   : nglog::internal::LogMessageVoidify() & SYSLOG(severity)

#define LOG_ASSERT(condition) \
  LOG_IF(FATAL, !(condition)) << "Assert failed: " #condition
#define SYSLOG_ASSERT(condition) \
  SYSLOG_IF(FATAL, !(condition)) << "Assert failed: " #condition

// CHECK dies with a fatal error if condition is not true.  It is *not*
// controlled by DCHECK_IS_ON(), so the check will be executed regardless of
// compilation mode.  Therefore, it is safe to do things like:
//    CHECK(fp->Write(x) == 4)
#define CHECK(condition)                                      \
  LOG_IF(FATAL, NGLOG_PREDICT_BRANCH_NOT_TAKEN(!(condition))) \
      << "Check failed: " #condition " "

// Helper macro for binary operators.
// Don't use this macro directly in your code, use CHECK_EQ et al below.

#if defined(STATIC_ANALYSIS)
// Only for static analysis tool to know that it is equivalent to assert
#  define CHECK_OP_LOG(name, op, val1, val2, log) CHECK((val1)op(val2))
#elif DCHECK_IS_ON()
// In debug mode, avoid constructing CheckOpStrings if possible,
// to reduce the overhead of CHECK statements by 2x.
// Real DCHECK-heavy tests have seen 1.5x speedups.

// The meaning of "string" might be different between now and
// when this macro gets invoked (e.g., if someone is experimenting
// with other string implementations that get defined after this
// file is included).  Save the current meaning now and use it
// in the macro.
namespace nglog {
namespace internal {
using _Check_string = std::string;
}  // namespace internal
}  // namespace nglog
#  define CHECK_OP_LOG(name, op, val1, val2, log)                    \
    while (std::unique_ptr<nglog::internal::_Check_string> _result = \
               nglog::internal::Check##name##Impl(                   \
                   nglog::internal::GetReferenceableValue(val1),     \
                   nglog::internal::GetReferenceableValue(val2),     \
                   #val1 " " #op " " #val2))                         \
    log(__FILE__, __LINE__,                                          \
        nglog::internal::CheckOpString(std::move(_result)))          \
        .stream()
#else
// In optimized mode, use CheckOpString to hint to compiler that
// the while condition is unlikely.
#  define CHECK_OP_LOG(name, op, val1, val2, log)                \
    while (nglog::internal::CheckOpString _result =              \
               nglog::internal::Check##name##Impl(               \
                   nglog::internal::GetReferenceableValue(val1), \
                   nglog::internal::GetReferenceableValue(val2), \
                   #val1 " " #op " " #val2))                     \
    log(__FILE__, __LINE__, _result).stream()
#endif  // STATIC_ANALYSIS, DCHECK_IS_ON()

#if NGLOG_STRIP_LOG <= 3
#  define CHECK_OP(name, op, val1, val2) \
    CHECK_OP_LOG(name, op, val1, val2, nglog::LogMessageFatal)
#else
#  define CHECK_OP(name, op, val1, val2) \
    CHECK_OP_LOG(name, op, val1, val2, nglog::NullStreamFatal)
#endif  // NGLOG_STRIP_LOG <= 3

// Equality/Inequality checks - compare two values, and log a FATAL message
// including the two values when the result is not as expected.  The values
// must have operator<<(ostream, ...) defined.
//
// You may append to the error message like so:
//   CHECK_NE(1, 2) << ": The world must be ending!";
//
// We are very careful to ensure that each argument is evaluated exactly
// once, and that anything which is legal to pass as a function argument is
// legal here.  In particular, the arguments may be temporary expressions
// which will end up being destroyed at the end of the apparent statement,
// for example:
//   CHECK_EQ(string("abc")[1], 'b');
//
// WARNING: These don't compile correctly if one of the arguments is a pointer
// and the other is nullptr. To work around this, simply static_cast nullptr to
// the type of the desired pointer.

#define CHECK_EQ(val1, val2) CHECK_OP(_EQ, ==, val1, val2)
#define CHECK_NE(val1, val2) CHECK_OP(_NE, !=, val1, val2)
#define CHECK_LE(val1, val2) CHECK_OP(_LE, <=, val1, val2)
#define CHECK_LT(val1, val2) CHECK_OP(_LT, <, val1, val2)
#define CHECK_GE(val1, val2) CHECK_OP(_GE, >=, val1, val2)
#define CHECK_GT(val1, val2) CHECK_OP(_GT, >, val1, val2)

// Check that the input is non nullptr.  This very useful in constructor
// initializer lists.

#define CHECK_NOTNULL(val)                          \
  nglog::internal::CheckNotNull(__FILE__, __LINE__, \
                                "'" #val "' Must be non nullptr", (val))

// Helper macro for string comparisons.
// Don't use this macro directly in your code, use CHECK_STREQ et al below.
#define CHECK_STROP(func, op, expected, s1, s2)            \
  while (nglog::internal::CheckOpString _result =          \
             nglog::internal::Check##func##expected##Impl( \
                 (s1), (s2), #s1 " " #op " " #s2))         \
  LOG(FATAL) << (*_result.str_)

// String (char*) equality/inequality checks.
// CASE versions are case-insensitive.
//
// Note that "s1" and "s2" may be temporary strings which are destroyed
// by the compiler at the end of the current "full expression"
// (e.g. CHECK_STREQ(Foo().c_str(), Bar().c_str())).

#define CHECK_STREQ(s1, s2) CHECK_STROP(strcmp, ==, true, s1, s2)
#define CHECK_STRNE(s1, s2) CHECK_STROP(strcmp, !=, false, s1, s2)
#define CHECK_STRCASEEQ(s1, s2) CHECK_STROP(strcasecmp, ==, true, s1, s2)
#define CHECK_STRCASENE(s1, s2) CHECK_STROP(strcasecmp, !=, false, s1, s2)

#define CHECK_INDEX(I, A) CHECK(I < (sizeof(A) / sizeof(A[0])))
#define CHECK_BOUND(B, A) CHECK(B <= (sizeof(A) / sizeof(A[0])))

#define CHECK_DOUBLE_EQ(val1, val2)                \
  do {                                             \
    CHECK_LE((val1), (val2) + 0.000000000000001L); \
    CHECK_GE((val1), (val2) - 0.000000000000001L); \
  } while (0)

#define CHECK_NEAR(val1, val2, margin)   \
  do {                                   \
    CHECK_LE((val1), (val2) + (margin)); \
    CHECK_GE((val1), (val2) - (margin)); \
  } while (0)

// perror()..googly style!
//
// PLOG() and PLOG_IF() and PCHECK() behave exactly like their LOG* and
// CHECK equivalents with the addition that they postpend a description
// of the current state of errno to their output lines.

#define PLOG(severity) NGLOG_PLOG(severity, 0).stream()

#define NGLOG_PLOG(severity, counter)                                   \
  nglog::ErrnoLogMessage(__FILE__, __LINE__, NGLOG_##severity, counter, \
                         &nglog::LogMessage::SendToLog)

#define PLOG_IF(severity, condition) \
  static_cast<void>(0),              \
      !(condition) ? (void)0         \
                   : nglog::internal::LogMessageVoidify() & PLOG(severity)

// A CHECK() macro that postpends errno if the condition is false. E.g.
//
// if (poll(fds, nfds, timeout) == -1) { PCHECK(errno == EINTR); ... }
#define PCHECK(condition)                                      \
  PLOG_IF(FATAL, NGLOG_PREDICT_BRANCH_NOT_TAKEN(!(condition))) \
      << "Check failed: " #condition " "

// A CHECK() macro that lets you assert the success of a function that
// returns -1 and sets errno in case of an error. E.g.
//
// CHECK_ERR(mkdir(path, 0700));
//
// or
//
// int fd = open(filename, flags); CHECK_ERR(fd) << ": open " << filename;
#define CHECK_ERR(invocation)                                        \
  PLOG_IF(FATAL, NGLOG_PREDICT_BRANCH_NOT_TAKEN((invocation) == -1)) \
      << #invocation

// Use macro expansion to create, for each use of LOG_EVERY_N(), static
// variables with the __LINE__ expansion as part of the variable name.
#define LOG_EVERY_N_VARNAME(base, line) LOG_EVERY_N_VARNAME_CONCAT(base, line)
#define LOG_EVERY_N_VARNAME_CONCAT(base, line) base##line

#define LOG_OCCURRENCES LOG_EVERY_N_VARNAME(occurrences_, __LINE__)
#define LOG_OCCURRENCES_MOD_N LOG_EVERY_N_VARNAME(occurrences_mod_n_, __LINE__)

#define LOG_TIME_PERIOD LOG_EVERY_N_VARNAME(timePeriod_, __LINE__)
#define LOG_PREVIOUS_TIME_RAW LOG_EVERY_N_VARNAME(previousTimeRaw_, __LINE__)
#define LOG_TIME_DELTA LOG_EVERY_N_VARNAME(deltaTime_, __LINE__)
#define LOG_CURRENT_TIME LOG_EVERY_N_VARNAME(currentTime_, __LINE__)
#define LOG_PREVIOUS_TIME LOG_EVERY_N_VARNAME(previousTime_, __LINE__)
#define SOME_KIND_OF_LOG_EVERY_T(severity, seconds)                            \
  constexpr std::chrono::nanoseconds LOG_TIME_PERIOD =                         \
      std::chrono::duration_cast<std::chrono::nanoseconds>(                    \
          std::chrono::duration<double>(seconds));                             \
  static std::atomic<std::int64_t> LOG_PREVIOUS_TIME_RAW;                      \
  NGLOG_IFDEF_THREAD_SANITIZER(AnnotateBenignRaceSized(                        \
      __FILE__, __LINE__, &LOG_TIME_PERIOD, sizeof(std::int64_t), ""));        \
  NGLOG_IFDEF_THREAD_SANITIZER(AnnotateBenignRaceSized(                        \
      __FILE__, __LINE__, &LOG_PREVIOUS_TIME_RAW, sizeof(std::int64_t), ""));  \
  const auto LOG_CURRENT_TIME =                                                \
      std::chrono::duration_cast<std::chrono::nanoseconds>(                    \
          std::chrono::steady_clock::now().time_since_epoch());                \
  const auto LOG_PREVIOUS_TIME =                                               \
      LOG_PREVIOUS_TIME_RAW.load(std::memory_order_relaxed);                   \
  const auto LOG_TIME_DELTA =                                                  \
      LOG_CURRENT_TIME - std::chrono::nanoseconds(LOG_PREVIOUS_TIME);          \
  if (LOG_TIME_DELTA > LOG_TIME_PERIOD)                                        \
    LOG_PREVIOUS_TIME_RAW.store(                                               \
        std::chrono::duration_cast<std::chrono::nanoseconds>(LOG_CURRENT_TIME) \
            .count(),                                                          \
        std::memory_order_relaxed);                                            \
  if (LOG_TIME_DELTA > LOG_TIME_PERIOD)                                        \
  nglog::LogMessage(__FILE__, __LINE__, NGLOG_##severity).stream()

#define SOME_KIND_OF_LOG_EVERY_N(severity, n, what_to_do)                  \
  static std::atomic<int> LOG_OCCURRENCES(0), LOG_OCCURRENCES_MOD_N(0);    \
  NGLOG_IFDEF_THREAD_SANITIZER(AnnotateBenignRaceSized(                    \
      __FILE__, __LINE__, &LOG_OCCURRENCES, sizeof(int), ""));             \
  NGLOG_IFDEF_THREAD_SANITIZER(AnnotateBenignRaceSized(                    \
      __FILE__, __LINE__, &LOG_OCCURRENCES_MOD_N, sizeof(int), ""));       \
  ++LOG_OCCURRENCES;                                                       \
  if (++LOG_OCCURRENCES_MOD_N > n) LOG_OCCURRENCES_MOD_N -= n;             \
  if (LOG_OCCURRENCES_MOD_N == 1)                                          \
  nglog::LogMessage(__FILE__, __LINE__, NGLOG_##severity, LOG_OCCURRENCES, \
                    &what_to_do)                                           \
      .stream()

#define SOME_KIND_OF_LOG_IF_EVERY_N(severity, condition, n, what_to_do)       \
  static std::atomic<int> LOG_OCCURRENCES(0), LOG_OCCURRENCES_MOD_N(0);       \
  NGLOG_IFDEF_THREAD_SANITIZER(AnnotateBenignRaceSized(                       \
      __FILE__, __LINE__, &LOG_OCCURRENCES, sizeof(int), ""));                \
  NGLOG_IFDEF_THREAD_SANITIZER(AnnotateBenignRaceSized(                       \
      __FILE__, __LINE__, &LOG_OCCURRENCES_MOD_N, sizeof(int), ""));          \
  ++LOG_OCCURRENCES;                                                          \
  if ((condition) &&                                                          \
      ((LOG_OCCURRENCES_MOD_N = (LOG_OCCURRENCES_MOD_N + 1) % n) == (1 % n))) \
  nglog::LogMessage(__FILE__, __LINE__, NGLOG_##severity, LOG_OCCURRENCES,    \
                    &what_to_do)                                              \
      .stream()

#define SOME_KIND_OF_PLOG_EVERY_N(severity, n, what_to_do)              \
  static std::atomic<int> LOG_OCCURRENCES(0), LOG_OCCURRENCES_MOD_N(0); \
  NGLOG_IFDEF_THREAD_SANITIZER(AnnotateBenignRaceSized(                 \
      __FILE__, __LINE__, &LOG_OCCURRENCES, sizeof(int), ""));          \
  NGLOG_IFDEF_THREAD_SANITIZER(AnnotateBenignRaceSized(                 \
      __FILE__, __LINE__, &LOG_OCCURRENCES_MOD_N, sizeof(int), ""));    \
  ++LOG_OCCURRENCES;                                                    \
  if (++LOG_OCCURRENCES_MOD_N > n) LOG_OCCURRENCES_MOD_N -= n;          \
  if (LOG_OCCURRENCES_MOD_N == 1)                                       \
  nglog::ErrnoLogMessage(__FILE__, __LINE__, NGLOG_##severity,          \
                         LOG_OCCURRENCES, &what_to_do)                  \
      .stream()

#define SOME_KIND_OF_LOG_FIRST_N(severity, n, what_to_do)                  \
  static std::atomic<int> LOG_OCCURRENCES(0);                              \
  NGLOG_IFDEF_THREAD_SANITIZER(AnnotateBenignRaceSized(                    \
      __FILE__, __LINE__, &LOG_OCCURRENCES, sizeof(int), ""));             \
  if (LOG_OCCURRENCES <= n) ++LOG_OCCURRENCES;                             \
  if (LOG_OCCURRENCES <= n)                                                \
  nglog::LogMessage(__FILE__, __LINE__, NGLOG_##severity, LOG_OCCURRENCES, \
                    &what_to_do)                                           \
      .stream()
#define LOG_EVERY_N(severity, n) \
  SOME_KIND_OF_LOG_EVERY_N(severity, (n), nglog::LogMessage::SendToLog)

#define LOG_EVERY_T(severity, T) SOME_KIND_OF_LOG_EVERY_T(severity, (T))

#define SYSLOG_EVERY_N(severity, n) \
  SOME_KIND_OF_LOG_EVERY_N(severity, (n), nglog::LogMessage::SendToSyslogAndLog)

#define PLOG_EVERY_N(severity, n) \
  SOME_KIND_OF_PLOG_EVERY_N(severity, (n), nglog::LogMessage::SendToLog)

#define LOG_FIRST_N(severity, n) \
  SOME_KIND_OF_LOG_FIRST_N(severity, (n), nglog::LogMessage::SendToLog)

#define LOG_IF_EVERY_N(severity, condition, n)            \
  SOME_KIND_OF_LOG_IF_EVERY_N(severity, (condition), (n), \
                              nglog::LogMessage::SendToLog)

#ifdef NGLOG_NO_ABBREVIATED_SEVERITIES
// wingdi.h defines ERROR to be 0. When we call LOG(ERROR), it gets
// substituted with 0, and it expands to NGLOG_COMPACT_LOG_0. To allow us
// to keep using this syntax, we define this macro to do the same thing
// as NGLOG_COMPACT_LOG_ERROR.
#  define NGLOG_COMPACT_LOG_0 NGLOG_COMPACT_LOG_ERROR
#  define SYSLOG_0 SYSLOG_ERROR
#  define LOG_TO_STRING_0 LOG_TO_STRING_ERROR
// Needed for LOG_IS_ON(ERROR).
namespace nglog {
NGLOG_INLINE_VARIABLE
constexpr LogSeverity NGLOG_0 = NGLOG_ERROR;
}  // namespace nglog
#else
// Users may include windows.h after logging.h without
// NGLOG_NO_ABBREVIATED_SEVERITIES nor WIN32_LEAN_AND_MEAN.
// For this case, we cannot detect if ERROR is defined before users
// actually use ERROR. Let's make an undefined symbol to warn users.
#  define NGLOG_ERROR_MSG \
    ERROR_macro_is_defined_Define_NGLOG_NO_ABBREVIATED_SEVERITIES_before_including_logging_h_See_the_document_for_detail
#  define NGLOG_COMPACT_LOG_0 NGLOG_ERROR_MSG
#  define SYSLOG_0 NGLOG_ERROR_MSG
#  define LOG_TO_STRING_0 NGLOG_ERROR_MSG
#  define NGLOG_0 NGLOG_ERROR_MSG
#endif

// Plus some debug-logging macros that get compiled to nothing for production

#if DCHECK_IS_ON()

#  define DLOG(severity) LOG(severity)
#  define DVLOG(verboselevel) VLOG(verboselevel)
#  define DLOG_IF(severity, condition) LOG_IF(severity, condition)
#  define DLOG_EVERY_N(severity, n) LOG_EVERY_N(severity, n)
#  define DLOG_IF_EVERY_N(severity, condition, n) \
    LOG_IF_EVERY_N(severity, condition, n)
#  define DLOG_FIRST_N(severity, n) LOG_FIRST_N(severity, n)
#  define DLOG_EVERY_T(severity, T) LOG_EVERY_T(severity, T)
#  define DLOG_ASSERT(condition) LOG_ASSERT(condition)

// debug-only checking.  executed if DCHECK_IS_ON().
#  define DCHECK(condition) CHECK(condition)
#  define DCHECK_EQ(val1, val2) CHECK_EQ(val1, val2)
#  define DCHECK_NE(val1, val2) CHECK_NE(val1, val2)
#  define DCHECK_LE(val1, val2) CHECK_LE(val1, val2)
#  define DCHECK_LT(val1, val2) CHECK_LT(val1, val2)
#  define DCHECK_GE(val1, val2) CHECK_GE(val1, val2)
#  define DCHECK_GT(val1, val2) CHECK_GT(val1, val2)
#  define DCHECK_NOTNULL(val) CHECK_NOTNULL(val)
#  define DCHECK_STREQ(str1, str2) CHECK_STREQ(str1, str2)
#  define DCHECK_STRCASEEQ(str1, str2) CHECK_STRCASEEQ(str1, str2)
#  define DCHECK_STRNE(str1, str2) CHECK_STRNE(str1, str2)
#  define DCHECK_STRCASENE(str1, str2) CHECK_STRCASENE(str1, str2)

#else  // !DCHECK_IS_ON()

#  define DLOG(severity) NGLOG_LOG_IF(severity, false)

#  define DVLOG(verboselevel) \
    NGLOG_LOG_IF(INFO, false && VLOG_IS_ON(verboselevel))

#  define DLOG_IF(severity, condition) \
    NGLOG_LOG_IF(severity, false && (condition))

#  define DLOG_EVERY_N(severity, n) NGLOG_LOG_IF(severity, false)

#  define DLOG_IF_EVERY_N(severity, condition, n) \
    NGLOG_LOG_IF(severity, false && (condition))

#  define DLOG_FIRST_N(severity, n) NGLOG_LOG_IF(severity, false)

#  define DLOG_EVERY_T(severity, T) NGLOG_LOG_IF(severity, false)

#  define DLOG_ASSERT(condition) NGLOG_LOG_IF(FATAL, false && (condition))

// MSVC warning C4127: conditional expression is constant
#  define DCHECK(condition)               \
    NGLOG_MSVC_PUSH_DISABLE_WARNING(4127) \
    while (false) NGLOG_MSVC_POP_WARNING() CHECK(condition)

#  define DCHECK_EQ(val1, val2)           \
    NGLOG_MSVC_PUSH_DISABLE_WARNING(4127) \
    while (false) NGLOG_MSVC_POP_WARNING() CHECK_EQ(val1, val2)

#  define DCHECK_NE(val1, val2)           \
    NGLOG_MSVC_PUSH_DISABLE_WARNING(4127) \
    while (false) NGLOG_MSVC_POP_WARNING() CHECK_NE(val1, val2)

#  define DCHECK_LE(val1, val2)           \
    NGLOG_MSVC_PUSH_DISABLE_WARNING(4127) \
    while (false) NGLOG_MSVC_POP_WARNING() CHECK_LE(val1, val2)

#  define DCHECK_LT(val1, val2)           \
    NGLOG_MSVC_PUSH_DISABLE_WARNING(4127) \
    while (false) NGLOG_MSVC_POP_WARNING() CHECK_LT(val1, val2)

#  define DCHECK_GE(val1, val2)           \
    NGLOG_MSVC_PUSH_DISABLE_WARNING(4127) \
    while (false) NGLOG_MSVC_POP_WARNING() CHECK_GE(val1, val2)

#  define DCHECK_GT(val1, val2)           \
    NGLOG_MSVC_PUSH_DISABLE_WARNING(4127) \
    while (false) NGLOG_MSVC_POP_WARNING() CHECK_GT(val1, val2)

// You may see warnings in release mode if you don't use the return
// value of DCHECK_NOTNULL. Please just use DCHECK for such cases.
#  define DCHECK_NOTNULL(val) (val)

#  define DCHECK_STREQ(str1, str2)        \
    NGLOG_MSVC_PUSH_DISABLE_WARNING(4127) \
    while (false) NGLOG_MSVC_POP_WARNING() CHECK_STREQ(str1, str2)

#  define DCHECK_STRCASEEQ(str1, str2)    \
    NGLOG_MSVC_PUSH_DISABLE_WARNING(4127) \
    while (false) NGLOG_MSVC_POP_WARNING() CHECK_STRCASEEQ(str1, str2)

#  define DCHECK_STRNE(str1, str2)        \
    NGLOG_MSVC_PUSH_DISABLE_WARNING(4127) \
    while (false) NGLOG_MSVC_POP_WARNING() CHECK_STRNE(str1, str2)

#  define DCHECK_STRCASENE(str1, str2)    \
    NGLOG_MSVC_PUSH_DISABLE_WARNING(4127) \
    while (false) NGLOG_MSVC_POP_WARNING() CHECK_STRCASENE(str1, str2)

#endif  // DCHECK_IS_ON()

// Log only in verbose mode.

#define VLOG(verboselevel) LOG_IF(INFO, VLOG_IS_ON(verboselevel))

#define VLOG_IF(verboselevel, condition) \
  LOG_IF(INFO, (condition) && VLOG_IS_ON(verboselevel))

#define VLOG_EVERY_N(verboselevel, n) \
  LOG_IF_EVERY_N(INFO, VLOG_IS_ON(verboselevel), n)

#define VLOG_IF_EVERY_N(verboselevel, condition, n) \
  LOG_IF_EVERY_N(INFO, (condition) && VLOG_IS_ON(verboselevel), n)

// A macro alternative of LogAtLevel. New code may want to use this
// version since there are two advantages: 1. this version outputs the
// file name and the line number where this macro is put like other
// LOG macros, 2. this macro can be used as C++ stream.
#define LOG_AT_LEVEL(severity) \
  nglog::LogMessage(__FILE__, __LINE__, severity).stream()

#endif  // NGLOG_LOGGING_H
