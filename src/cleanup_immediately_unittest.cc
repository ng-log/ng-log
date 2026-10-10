// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "base/commandlineflags.h"
#include "mock-log.h"
#include "ng-log/logging.h"
#include "ng-log/raw_logging.h"
#include "testing_utilities.h"

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

TEST(CleanImmediately, logging) {
  using namespace std::chrono_literals;
  nglog::SetLogFilenameExtension(".foobar");
  nglog::EnableLogCleaner(0h);

  for (unsigned i = 0; i < 1000; ++i) {
    LOG(INFO) << "cleanup test";
    LOG(WARNING) << "cleanup test";
    LOG(ERROR) << "cleanup test";
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
