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

#ifndef NGLOG_FLAGS_H
#define NGLOG_FLAGS_H

#include <string>

#if defined(NGLOG_USE_GFLAGS)
#  include <gflags/gflags.h>
#endif

#if defined(NGLOG_USE_EXPORT)
#  include "ng-log/export.h"
#endif

#if !defined(NGLOG_EXPORT)
#  error <ng-log/flags.h> was not included correctly. See the documentation for how to consume the library.
#endif

#include "ng-log/platform.h"
#include "ng-log/types.h"

#pragma push_macro("DECLARE_VARIABLE")
#pragma push_macro("DECLARE_bool")
#pragma push_macro("DECLARE_string")
#pragma push_macro("DECLARE_int32")
#pragma push_macro("DECLARE_uint32")

#ifdef DECLARE_VARIABLE
#  undef DECLARE_VARIABLE
#endif

#ifdef DECLARE_bool
#  undef DECLARE_bool
#endif

#ifdef DECLARE_string
#  undef DECLARE_string
#endif

#ifdef DECLARE_int32
#  undef DECLARE_int32
#endif

#ifdef DECLARE_uint32
#  undef DECLARE_uint32
#endif

#ifndef DECLARE_VARIABLE
#  define DECLARE_VARIABLE(type, shorttype, name, tn) \
    namespace fL##shorttype {                         \
      extern NGLOG_EXPORT type FLAGS_##name;          \
    }                                                 \
    using fL##shorttype::FLAGS_##name

// bool specialization
#  define DECLARE_bool(name) DECLARE_VARIABLE(bool, B, name, bool)

// int32 specialization
#  define DECLARE_int32(name) DECLARE_VARIABLE(nglog::int32, I, name, int32)

#  if !defined(DECLARE_uint32)
// uint32 specialization
#    define DECLARE_uint32(name) \
      DECLARE_VARIABLE(nglog::uint32, U, name, uint32)
#  endif  // !defined(DECLARE_uint32) && !defined(NGLOG_USE_GFLAGS)

// Special case for string, because we have to specify the namespace
// std::string, which doesn't play nicely with our FLAG__namespace hackery.
#  define DECLARE_string(name)                     \
    namespace fLS {                                \
    extern NGLOG_EXPORT std::string& FLAGS_##name; \
    }                                              \
    using fLS::FLAGS_##name
#endif

DECLARE_int32(logemaillevel);
DECLARE_int32(logcleansecs);

#ifdef NGLOG_OS_LINUX
DECLARE_bool(drop_log_memory);
#endif
DECLARE_string(alsologtoemail);
DECLARE_string(log_backtrace_at);

// Set whether appending a timestamp to the log file name
DECLARE_bool(timestamp_in_logfile_name);

// Set whether log messages go to stdout instead of logfiles
DECLARE_bool(logtostdout);

// Set color messages logged to stdout (if supported by terminal).
DECLARE_bool(colorlogtostdout);

// Set whether log messages go to stderr instead of logfiles
DECLARE_bool(logtostderr);

// Set whether log messages go to stderr in addition to logfiles.
DECLARE_bool(alsologtostderr);

// Set color messages logged to stderr (if supported by terminal).
DECLARE_bool(colorlogtostderr);

// Log messages at a level >= this flag are automatically sent to
// stderr in addition to log files.
DECLARE_int32(stderrthreshold);

// Set whether the log file header should be written upon creating a file.
DECLARE_bool(log_file_header);

// Set whether the log prefix should be prepended to each line of output.
DECLARE_bool(log_prefix);

// Set whether the year should be included in the log prefix.
DECLARE_bool(log_year_in_prefix);

// Log messages at a level <= this flag are buffered.
// Log messages at a higher level are flushed immediately.
DECLARE_int32(logbuflevel);

// Sets the maximum number of seconds which logs may be buffered for.
DECLARE_int32(logbufsecs);

// Log suppression level: messages logged at a lower level than this
// are suppressed.
DECLARE_int32(minloglevel);

// If specified, logfiles are written into this directory instead of the
// default logging directory.
DECLARE_string(log_dir);

// Set the log file mode.
DECLARE_int32(logfile_mode);

// Sets the path of the directory into which to put additional links
// to the log files.
DECLARE_string(log_link);

DECLARE_int32(v);  // in vlog_is_on.cc

DECLARE_string(vmodule);  // also in vlog_is_on.cc

// Sets the maximum log file size (in MB).
DECLARE_uint32(max_log_size);

// Sets whether to avoid logging to the disk if the disk is full.
DECLARE_bool(stop_logging_if_full_disk);

// Use UTC time for logging
DECLARE_bool(log_utc_time);

// Mailer used to send logging email
DECLARE_string(logmailer);

DECLARE_bool(symbolize_stacktrace);

#pragma pop_macro("DECLARE_VARIABLE")
#pragma pop_macro("DECLARE_bool")
#pragma pop_macro("DECLARE_string")
#pragma pop_macro("DECLARE_int32")
#pragma pop_macro("DECLARE_uint32")

#endif  // NGLOG_FLAGS_H
