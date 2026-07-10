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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "base/commandlineflags.h"
#include "ng-log/logging.h"
#include "testing_utilities.h"

#ifdef NGLOG_USE_GFLAGS
#  include <gflags/gflags.h>
using namespace GFLAGS_NAMESPACE;
#endif

using namespace nglog;

TEST(LogToStdout, HonorsLogbuflevel) {
  const bool saved_logtostdout = FLAGS_logtostdout;
  const bool saved_colorlogtostdout = FLAGS_colorlogtostdout;
  const auto saved_logbuflevel = FLAGS_logbuflevel;

  CaptureTestStdout();

  // stdout now refers to a regular file; make sure it is fully buffered so
  // that messages which are not explicitly flushed stay in the stdio buffer.
  fflush(stdout);
  setvbuf(stdout, nullptr, _IOFBF, 8192);

  FLAGS_logtostdout = true;
  FLAGS_colorlogtostdout = false;
  FLAGS_logbuflevel = NGLOG_INFO;  // buffer INFO, flush WARNING and above

  LOG(INFO) << "buffered info message";

  EXPECT_THAT(GetCapturedTestStdout(),
              testing::HasSubstr("buffered info message"));

  CaptureTestStdout();

  LOG(WARNING) << "unbuffered warning message";

  const std::string captured = GetCapturedTestStdout();

  EXPECT_THAT(captured, testing::HasSubstr("unbuffered warning message"));
  // Flushing the stream also flushes all previously buffered messages.
  EXPECT_THAT(captured,
              testing::Not(testing::HasSubstr("buffered info message")));

  FLAGS_logtostdout = saved_logtostdout;
  FLAGS_colorlogtostdout = saved_colorlogtostdout;
  FLAGS_logbuflevel = saved_logbuflevel;
}

int main(int argc, char** argv) {
  FLAGS_colorlogtostderr = false;

  // Make sure stderr is not buffered as stderr seems to be buffered
  // on recent windows.
  setbuf(stderr, nullptr);

  testing::InitGoogleTest(&argc, argv);
  testing::InitGoogleMock(&argc, argv);

#ifdef NGLOG_USE_GFLAGS
  ParseCommandLineFlags(&argc, &argv, true);
#endif

  return RUN_ALL_TESTS();
}
