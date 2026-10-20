// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <ostream>
#include <string>

#include "cleanup_test.h"
#include "ng-log/flags.h"
#include "ng-log/logging.h"

TEST(CleanImmediatelyWithAbsolutePrefix, logging) {
  using namespace std::chrono_literals;
  constexpr unsigned kInfoMessageCount = 1000;
  constexpr unsigned kErrorMessageCount = 10;
  nglog::EnableLogCleaner(0h);
  nglog::SetLogFilenameExtension(".barfoo");
  // The driver passes an absolute log directory with a trailing separator.
  const std::string log_dir = nglog::GetFlag(FLAGS_log_dir);
  ASSERT_THAT(log_dir, testing::Not(testing::IsEmpty()));
  nglog::SetLogDestination(NGLOG_INFO, (log_dir + "test_cleanup_").c_str());

  for (unsigned i = 0; i < kInfoMessageCount; ++i) {
    LOG(INFO) << nglog::kCleanupTestMessage;
  }

  for (unsigned i = 0; i < kErrorMessageCount; ++i) {
    LOG(ERROR) << nglog::kCleanupTestMessage;
  }

  nglog::DisableLogCleaner();
}

int main(int argc, char** argv) { return nglog::CleanupTestMain(argc, argv); }
