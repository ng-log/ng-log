// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause

#include <gtest/gtest.h>

#include <chrono>
#include <ostream>
#include <string>

#include "cleanup_test.h"
#include "ng-log/logging.h"

TEST(CleanImmediatelyWithAbsolutePrefix, logging) {
  using namespace std::chrono_literals;
  constexpr unsigned kInfoMessageCount = 1000;
  constexpr unsigned kErrorMessageCount = 10;
  nglog::EnableLogCleaner(0h);
  nglog::SetLogFilenameExtension(".barfoo");
  nglog::SetLogDestination(NGLOG_INFO, "test_cleanup_");

  for (unsigned i = 0; i < kInfoMessageCount; ++i) {
    LOG(INFO) << nglog::kCleanupTestMessage;
  }

  for (unsigned i = 0; i < kErrorMessageCount; ++i) {
    LOG(ERROR) << nglog::kCleanupTestMessage;
  }

  nglog::DisableLogCleaner();
}

int main(int argc, char** argv) { return nglog::CleanupTestMain(argc, argv); }
