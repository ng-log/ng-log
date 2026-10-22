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
#include "ng-log/logging.h"

TEST(CleanImmediatelyWithRelativePrefix, logging) {
  using namespace std::chrono_literals;
  constexpr unsigned kMessageCount = 1000;
  constexpr char kExtension[] = ".relativefoo";
  constexpr char kPrefix[] = "test_cleanup_";
  constexpr char kSubdir[] = "test_subdir";
  nglog::SetLogFilenameExtension(kExtension);
  nglog::SetLogDestination(NGLOG_INFO,
                           (std::string(kSubdir) + '/' + kPrefix).c_str());
  // Log files must exist in the log directory before they become overdue.
  nglog::EnableLogCleaner(nglog::kCleanupTestRetention);
  LOG(INFO) << nglog::kCleanupTestMessage;
  EXPECT_THAT(nglog::ListOwnLogFiles(kSubdir, kExtension),
              testing::ElementsAre(testing::StartsWith(kPrefix)));
  nglog::EnableLogCleaner(0h);

  for (unsigned i = 0; i < kMessageCount; ++i) {
    LOG(INFO) << nglog::kCleanupTestMessage;
  }

  nglog::DisableLogCleaner();
}

int main(int argc, char** argv) { return nglog::CleanupTestMain(argc, argv); }
