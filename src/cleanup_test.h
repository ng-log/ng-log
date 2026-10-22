// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Shared entry point of the log cleaner tests. Each test runs in its own
// executable whose log files are counted afterwards by a CMake script.

#ifndef NGLOG_SRC_CLEANUP_TEST_H_
#define NGLOG_SRC_CLEANUP_TEST_H_

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "internal/path.h"
#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "utilities.h"

namespace nglog {

//! Message written repeatedly to fill the log files.
constexpr const char* kCleanupTestMessage = "cleanup test";

// Overdue window which keeps the log files of a test run from being removed.
constexpr std::chrono::hours kCleanupTestRetention{24};

// Lists the names of the log files with the given extension that the current
// process created in a directory, without the directory. Timestamped log file
// names end with the process ID followed by the filename extension, which tells
// them apart from the log files of earlier runs.
inline std::vector<std::string> ListOwnLogFiles(const std::string& directory,
                                                const std::string& extension) {
  struct Context {
    std::string suffix;
    std::vector<std::string> names;
  };

  Context context{'.' + std::to_string(GetMainThreadPid()) + extension, {}};

  const bool listed = internal::ForEachDirectoryPath(
      directory,
      [](const char* entry, std::size_t length, void* data) {
        auto* const listing = static_cast<Context*>(data);
        const std::string name(entry, length);
        if (name.size() >= listing->suffix.size() &&
            name.compare(name.size() - listing->suffix.size(),
                         listing->suffix.size(), listing->suffix) == 0) {
          listing->names.push_back(name);
        }
        return true;
      },
      &context);
  EXPECT_TRUE(listed) << "cannot list the directory " << directory;

  return context.names;
}

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
