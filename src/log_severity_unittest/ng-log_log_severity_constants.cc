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
// Author: Sergiu Deitsch

#include <ng-log/logging.h>

#include <type_traits>

static_assert(
    std::is_same<typename std::remove_const<decltype(NGLOG_INFO)>::type,
                 nglog::LogSeverity>::value,
    "NGLOG_INFO must have type nglog::LogSeverity");
static_assert(
    std::is_same<typename std::remove_const<decltype(NGLOG_WARNING)>::type,
                 nglog::LogSeverity>::value,
    "NGLOG_WARNING must have type nglog::LogSeverity");
static_assert(
    std::is_same<typename std::remove_const<decltype(NGLOG_ERROR)>::type,
                 nglog::LogSeverity>::value,
    "NGLOG_ERROR must have type nglog::LogSeverity");
static_assert(
    std::is_same<typename std::remove_const<decltype(NGLOG_FATAL)>::type,
                 nglog::LogSeverity>::value,
    "NGLOG_FATAL must have type nglog::LogSeverity");

static_assert(NGLOG_INFO == static_cast<nglog::LogSeverity>(0),
              "NGLOG_INFO must have value 0");
static_assert(NGLOG_WARNING == static_cast<nglog::LogSeverity>(1),
              "NGLOG_WARNING must have value 1");
static_assert(NGLOG_ERROR == static_cast<nglog::LogSeverity>(2),
              "NGLOG_ERROR must have value 2");
static_assert(NGLOG_FATAL == static_cast<nglog::LogSeverity>(3),
              "NGLOG_FATAL must have value 3");

#ifndef NGLOG_NO_ABBREVIATED_SEVERITIES
static_assert(nglog::INFO == NGLOG_INFO, "INFO must equal NGLOG_INFO");
static_assert(nglog::WARNING == NGLOG_WARNING,
              "WARNING must equal NGLOG_WARNING");
static_assert(nglog::ERROR == NGLOG_ERROR, "ERROR must equal NGLOG_ERROR");
static_assert(nglog::FATAL == NGLOG_FATAL, "FATAL must equal NGLOG_FATAL");
#endif

int main() {
  // Must not compile
  LOG(0) << "type unsafe info";
  LOG(1) << "type unsafe info";
  LOG(2) << "type unsafe info";
  LOG(3) << "type unsafe info";
}
