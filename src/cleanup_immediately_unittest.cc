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

TEST(CleanImmediately, logging) {
  using namespace std::chrono_literals;
  constexpr unsigned kMessageCount = 1000;
  constexpr char kExtension[] = ".foobar";
  const std::string log_dir = nglog::GetFlag(FLAGS_log_dir);
  ASSERT_THAT(log_dir, testing::Not(testing::IsEmpty()));
  nglog::SetLogFilenameExtension(kExtension);
  // Log files must exist in the log directory before they become overdue.
  nglog::EnableLogCleaner(nglog::kCleanupTestRetention);
  LOG(ERROR) << nglog::kCleanupTestMessage;
  EXPECT_THAT(nglog::ListOwnLogFiles(log_dir, kExtension),
              testing::UnorderedElementsAre(testing::HasSubstr(".log.INFO."),
                                            testing::HasSubstr(".log.WARNING."),
                                            testing::HasSubstr(".log.ERROR.")));
  nglog::EnableLogCleaner(0h);

  for (unsigned i = 0; i < kMessageCount; ++i) {
    LOG(INFO) << nglog::kCleanupTestMessage;
    LOG(WARNING) << nglog::kCleanupTestMessage;
    LOG(ERROR) << nglog::kCleanupTestMessage;
  }

  nglog::DisableLogCleaner();
}

int main(int argc, char** argv) { return nglog::CleanupTestMain(argc, argv); }
