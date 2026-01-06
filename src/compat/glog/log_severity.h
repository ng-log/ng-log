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

#ifndef NGLOG_COMPAT_LOG_SEVERITY_H
#define NGLOG_COMPAT_LOG_SEVERITY_H

#include "ng-log/log_severity.h"

#pragma message( \
    "<glog/log_severity.h> is deprecated and will be removed in ng-log 0.9.0. Please use <ng-log/log_severity.h> instead.")

namespace google {

using LogSeverity [[deprecated("Use nglog::LogSeverity instead.")]] =
    nglog::LogSeverity;
[[deprecated("Use nglog::NUM_SEVERITIES instead.")]]
constexpr int NUM_SEVERITIES = nglog::NUM_SEVERITIES;

#ifndef GLOG_NO_ABBREVIATED_SEVERITIES
#  ifdef ERROR
#  error ERROR macro is defined. Define GLOG_NO_ABBREVIATED_SEVERITIES before including logging.h. See the document for detail.
#  endif
[[deprecated("Use nglog::ERROR instead.")]]
constexpr nglog::LogSeverity ERROR = NGLOG_ERROR;
[[deprecated("Use nglog::FATAL instead.")]]
constexpr nglog::LogSeverity FATAL = NGLOG_FATAL;
[[deprecated("Use nglog::INFO instead.")]]
constexpr nglog::LogSeverity INFO = NGLOG_INFO;
[[deprecated("Use nglog::WARNING instead.")]]
constexpr nglog::LogSeverity WARNING = NGLOG_WARNING;
#endif

}  // namespace google

[[deprecated("Use NGLOG_ERROR instead.")]]
constexpr nglog::LogSeverity GLOG_ERROR = NGLOG_ERROR;
[[deprecated("Use NGLOG_FATAL instead.")]]
constexpr nglog::LogSeverity GLOG_FATAL = NGLOG_FATAL;
[[deprecated("Use NGLOG_INFO instead.")]]
constexpr nglog::LogSeverity GLOG_INFO = NGLOG_INFO;
[[deprecated("Use NGLOG_WARNING instead.")]]
constexpr nglog::LogSeverity GLOG_WARNING = NGLOG_WARNING;

#endif  // NGLOG_COMPAT_LOG_SEVERITY_H
