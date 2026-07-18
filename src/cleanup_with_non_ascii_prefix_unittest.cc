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

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "base/commandlineflags.h"
#include "mock-log.h"
#include "ng-log/logging.h"
#include "ng-log/raw_logging.h"
#include "testing_utilities.h"

#ifdef NGLOG_OS_WINDOWS
#  include <direct.h>
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#endif

#ifdef NGLOG_USE_GFLAGS
#  include <gflags/gflags.h>
using namespace GFLAGS_NAMESPACE;
#endif

// Introduce several symbols from gmock.
using nglog::nglog_testing::ScopedMockLog;
using testing::_;
using testing::AllOf;
using testing::AnyNumber;
using testing::HasSubstr;
using testing::InitGoogleMock;
using testing::StrictMock;
using testing::StrNe;

using namespace nglog;

// The directory name deliberately contains non-ASCII characters to exercise
// the multi-byte to wide character path conversions performed by the log
// cleaner on Windows (see https://github.com/google/glog/issues/786). The
// bytes correspond to U+20AC (euro sign) in UTF-8. The directory is created
// here instead of by the test driver so that its name is encoded consistently
// with the narrow character strings the logging library passes to the
// operating system.
constexpr const char kLogDir[] = "cleanup_\xE2\x82\xAC_dir";

static void MakeLogDir(const char* name) {
#ifdef NGLOG_OS_WINDOWS
  _mkdir(name);
#else
  mkdir(name, 0777);
#endif
}

TEST(CleanImmediatelyWithNonAsciiPrefix, logging) {
  using namespace std::chrono_literals;
  MakeLogDir(kLogDir);
  nglog::EnableLogCleaner(0h);
  nglog::SetLogFilenameExtension(".nonasciifoo");
  nglog::SetLogDestination(NGLOG_INFO,
                           (std::string(kLogDir) + "/test_cleanup_").c_str());

  for (unsigned i = 0; i < 1000; ++i) {
    LOG(INFO) << "cleanup test";
  }

  nglog::DisableLogCleaner();
}

int main(int argc, char** argv) {
  FLAGS_colorlogtostderr = false;
  FLAGS_timestamp_in_logfile_name = true;
#ifdef NGLOG_USE_GFLAGS
  ParseCommandLineFlags(&argc, &argv, true);
#endif
  // Make sure stderr is not buffered as stderr seems to be buffered
  // on recent windows.
  setbuf(stderr, nullptr);

  // Test some basics before InitializeLogging:
  CaptureTestStderr();
  const std::string early_stderr = GetCapturedTestStderr();

  EXPECT_FALSE(IsLoggingInitialized());

  InitializeLogging(argv[0]);

  EXPECT_TRUE(IsLoggingInitialized());

  testing::InitGoogleTest(&argc, argv);
  InitGoogleMock(&argc, argv);

  // so that death tests run before we use threads
  CHECK_EQ(RUN_ALL_TESTS(), 0);
}
