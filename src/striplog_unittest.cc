// Copyright (c) 2023, Google Inc.
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
//
// Author: Sergey Ioffe

// The common part of the striplog tests.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iosfwd>
#include <string>

#include "base/commandlineflags.h"
#include "config.h"
#include "ng-log/logging.h"
#include "testing_utilities.h"

#ifndef NGLOG_STRIP_LOG
#  define NGLOG_STRIP_LOG 0
#endif

using namespace nglog;
using testing::HasSubstr;
using testing::Not;

namespace {
void handle_abort(int /*code*/) { std::exit(EXIT_FAILURE); }

constexpr char kInfoMessage[] = "strip info";
constexpr char kWarningMessage[] = "strip warning";
constexpr char kErrorMessage[] = "strip error";
}  // namespace

TEST(StripLog, LogsExpectedSeverities) {
  CaptureTestStderr();
  LOG(INFO) << kInfoMessage;
  LOG(WARNING) << kWarningMessage;
  LOG(ERROR) << kErrorMessage;
  const std::string output = GetCapturedTestStderr();

#if NGLOG_STRIP_LOG == 0
  EXPECT_THAT(output, HasSubstr(kInfoMessage));
  EXPECT_THAT(output, HasSubstr(kWarningMessage));
  EXPECT_THAT(output, HasSubstr(kErrorMessage));
#elif NGLOG_STRIP_LOG == 2
  EXPECT_THAT(output, Not(HasSubstr(kInfoMessage)));
  EXPECT_THAT(output, Not(HasSubstr(kWarningMessage)));
  EXPECT_THAT(output, HasSubstr(kErrorMessage));
#elif NGLOG_STRIP_LOG == 10
  EXPECT_THAT(output, Not(HasSubstr(kInfoMessage)));
  EXPECT_THAT(output, Not(HasSubstr(kWarningMessage)));
  EXPECT_THAT(output, Not(HasSubstr(kErrorMessage)));
#else
#  error Unsupported NGLOG_STRIP_LOG value
#endif
}

TEST(StripLog, FatalIsNotStripped) {
#if NGLOG_STRIP_LOG <= 3
  ASSERT_DEATH(LOG(FATAL) << "fatal log", "fatal log");
#else
  ASSERT_DEATH(LOG(FATAL) << "fatal log", "");
#endif
}

int main(int argc, char* argv[]) {
#if defined(_MSC_VER)
  // Avoid presenting an interactive dialog that will cause the test to time
  // out.
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif  // defined(_MSC_VER)
  std::signal(SIGABRT, handle_abort);

  FLAGS_logtostderr = true;
  InitializeLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);
  testing::InitGoogleMock(&argc, argv);
  return RUN_ALL_TESTS();
}
