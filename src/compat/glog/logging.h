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
// Author: Ray Sidney
//
// This file contains #include information about logging-related stuff.
// Pretty much everybody needs to #include this file so that they can
// log various happenings.
//
#ifndef NGLOG_COMPAT_LOGGING_H
#define NGLOG_COMPAT_LOGGING_H

#if defined(GLOG_STRIP_LOG)
#  define NGLOG_STRIP_LOG GLOG_STRIP_LOG
#endif  // defined(GLOG_STRIP_LOG)

#include "glog/export.h"
#include "glog/log_severity.h"
#include "glog/types.h"
#include "ng-log/logging.h"

#pragma message( \
    "<glog/logging.h> is deprecated and will be removed in ng-log 0.9.0. Please use <ng-log/logging.h> instead.")

namespace google {

using ErrnoLogMessage [[deprecated("Use nglog::ErrnoLogMessage instead.")]] =
    nglog::ErrnoLogMessage;
using logging_fail_func_t
    [[deprecated("Use nglog::logging_fail_func_t instead.")]] =
        nglog::logging_fail_func_t;
using LogMessage [[deprecated("Use nglog::LogMessage instead.")]] =
    nglog::LogMessage;
using LogMessageFatal [[deprecated("Use nglog::LogMessageFatal instead.")]] =
    nglog::LogMessageFatal;
using LogMessageTime [[deprecated("Use nglog::LogMessageTime instead.")]] =
    nglog::LogMessageTime;
using LogSink [[deprecated("Use nglog::LogSink instead.")]] = nglog::LogSink;
using PrefixFormatterCallback
    [[deprecated("Use nglog::PrefixFormatterCallback instead.")]] =
        nglog::PrefixFormatterCallback;

[[deprecated("Use nglog::InitializeLogging instead.")]]
NGLOG_COMPAT_EXPORT void InitGoogleLogging(const char* argv0);
[[deprecated("Use nglog::IsLoggingInitialized instead.")]]
NGLOG_COMPAT_EXPORT bool IsGoogleLoggingInitialized();
[[deprecated("Use nglog::ShutdownLogging instead.")]]
NGLOG_COMPAT_EXPORT void ShutdownGoogleLogging();
[[deprecated("Use nglog::EnableLogCleaner instead.")]]
NGLOG_COMPAT_EXPORT void EnableLogCleaner(const std::chrono::minutes& overdue);
[[deprecated("Use nglog::DisableLogCleaner instead.")]]
NGLOG_COMPAT_EXPORT void DisableLogCleaner();
[[deprecated("Use nglog::SetApplicationFingerprint instead.")]]
NGLOG_COMPAT_EXPORT void SetApplicationFingerprint(
    const std::string& fingerprint);
[[deprecated("Use nglog::FlushLogFiles instead.")]]
NGLOG_COMPAT_EXPORT void FlushLogFiles(LogSeverity min_severity);
[[deprecated("Use nglog::FlushLogFilesUnsafe instead.")]]
NGLOG_COMPAT_EXPORT void FlushLogFilesUnsafe(LogSeverity min_severity);
[[deprecated("Use nglog::SetLogDestination instead.")]]
NGLOG_COMPAT_EXPORT void SetLogDestination(LogSeverity severity,
                                           const char* base_filename);
[[deprecated("Use nglog::SetLogSymlink instead.")]]
NGLOG_COMPAT_EXPORT void SetLogSymlink(LogSeverity severity,
                                       const char* symlink_basename);
[[deprecated("Use nglog::AddLogSink instead.")]]
NGLOG_COMPAT_EXPORT void AddLogSink(LogSink* destination);
[[deprecated("Use nglog::RemoveLogSink instead.")]]
NGLOG_COMPAT_EXPORT void RemoveLogSink(LogSink* destination);
[[deprecated("Use nglog::InstallFailureSignalHandler instead.")]]
NGLOG_COMPAT_EXPORT void InstallFailureSignalHandler();
[[deprecated("Use nglog::IsFailureSignalHandlerInstalled instead.")]]
NGLOG_COMPAT_EXPORT bool IsFailureSignalHandlerInstalled();
[[deprecated("Use nglog::InstallFailureWriter instead.")]]
NGLOG_COMPAT_EXPORT void InstallFailureWriter(void (*writer)(const char* data,
                                                             size_t size));

[[deprecated("Use nglog::SetLogFilenameExtension instead.")]]
NGLOG_COMPAT_EXPORT void SetLogFilenameExtension(
    const char* filename_extension);

[[deprecated("Use nglog::SetStderrLogging instead.")]]
NGLOG_COMPAT_EXPORT void SetStderrLogging(LogSeverity min_severity);

[[deprecated("Use nglog::LogToStderr instead.")]]
NGLOG_COMPAT_EXPORT void LogToStderr();

[[deprecated("Use nglog::SetEmailLogging instead.")]]
NGLOG_COMPAT_EXPORT void SetEmailLogging(LogSeverity min_severity,
                                         const char* addresses);

[[deprecated("Use nglog::SendEmail instead.")]]
NGLOG_COMPAT_EXPORT bool SendEmail(const char* dest, const char* subject,
                                   const char* body);

[[deprecated("Use nglog::GetLoggingDirectories instead.")]]
NGLOG_COMPAT_EXPORT const std::vector<std::string>& GetLoggingDirectories();

[[deprecated("Use nglog::ReprintFatalMessage instead.")]]
NGLOG_COMPAT_EXPORT void ReprintFatalMessage();

[[deprecated("Use nglog::TruncateLogFile instead.")]]
NGLOG_COMPAT_EXPORT void TruncateLogFile(const char* path, uint64 limit,
                                         uint64 keep);

[[deprecated("Use nglog::TruncateStdoutStderr instead.")]]
NGLOG_COMPAT_EXPORT void TruncateStdoutStderr();

[[deprecated("Use nglog::GetLogSeverityName instead.")]]
NGLOG_COMPAT_EXPORT const char* GetLogSeverityName(LogSeverity severity);

}  // namespace google

#endif  // NGLOG_COMPAT_LOGGING_H
