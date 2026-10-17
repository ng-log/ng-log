// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Shared entry point of the log cleaner tests. Each test runs in its own
// executable whose log files are counted afterwards by a CMake script.

#ifndef NGLOG_SRC_CLEANUP_TEST_H_
#define NGLOG_SRC_CLEANUP_TEST_H_

#include <gtest/gtest.h>

#include <cstdio>

#include "ng-log/flags.h"
#include "ng-log/logging.h"

namespace nglog {

//! Message written repeatedly to fill the log files.
constexpr const char* kCleanupTestMessage = "cleanup test";

/**
 * Initializes logging with timestamped log file names and runs all tests.
 *
 * @return The result of @c RUN_ALL_TESTS().
 */
inline int CleanupTestMain(int argc, char** argv) {
  SetFlag(FLAGS_colorlogtostderr, false);
  SetFlag(FLAGS_timestamp_in_logfile_name, true);
  ParseCommandLineFlags(&argc, &argv, FlagParsingMode::kRemoveFlags);
  // Make sure stderr is not buffered as stderr seems to be buffered
  // on recent windows.
  std::setvbuf(stderr, nullptr, _IONBF, 0);

  InitializeLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}

}  // namespace nglog

#endif  // NGLOG_SRC_CLEANUP_TEST_H_
