// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>

#include "mock-log.h"
#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "ng-log/raw_logging.h"
#include "testing_utilities.h"

#ifdef NGLOG_OS_WINDOWS
#  include <direct.h>
#else
#  include <sys/stat.h>
#  include <sys/types.h>
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
  nglog::SetFlag(FLAGS_colorlogtostderr, false);
  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, true);
  nglog::ParseCommandLineFlags(&argc, &argv,
                               nglog::FlagParsingMode::kRemoveFlags);
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
