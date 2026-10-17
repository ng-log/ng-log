// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause

#include <chrono>

#include "cleanup_test.h"

TEST(CleanImmediatelyWithRelativePrefix, logging) {
  using namespace std::chrono_literals;
  constexpr unsigned kMessageCount = 1000;
  nglog::EnableLogCleaner(0h);
  nglog::SetLogFilenameExtension(".relativefoo");
  nglog::SetLogDestination(NGLOG_INFO, "test_subdir/test_cleanup_");

  for (unsigned i = 0; i < kMessageCount; ++i) {
    LOG(INFO) << nglog::kCleanupTestMessage;
  }

  nglog::DisableLogCleaner();
}

int main(int argc, char** argv) { return nglog::CleanupTestMain(argc, argv); }
