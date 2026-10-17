// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause

#include <chrono>

#include "cleanup_test.h"

TEST(CleanImmediately, logging) {
  using namespace std::chrono_literals;
  constexpr unsigned kMessageCount = 1000;
  nglog::SetLogFilenameExtension(".foobar");
  nglog::EnableLogCleaner(0h);

  for (unsigned i = 0; i < kMessageCount; ++i) {
    LOG(INFO) << nglog::kCleanupTestMessage;
    LOG(WARNING) << nglog::kCleanupTestMessage;
    LOG(ERROR) << nglog::kCleanupTestMessage;
  }

  nglog::DisableLogCleaner();
}

int main(int argc, char** argv) { return nglog::CleanupTestMain(argc, argv); }
