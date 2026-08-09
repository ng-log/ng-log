// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include <cstdlib>

#include <gtest/gtest.h>

#include "ng-log/flags.h"
#include "ng-log/logging.h"

using namespace nglog;

#if !defined(NGLOG_OS_WINDOWS)
TEST(Logging, PreInitializationWarningUsesFieldColors) {
  ASSERT_EQ(setenv("CLICOLOR_FORCE", "1", 1), 0);
  ASSERT_EQ(setenv("TERM", "xterm", 1), 0);
  unsetenv("NO_COLOR");

  FLAGS_colorlogtostderr = true;
  ASSERT_EXIT(
      {
        LOG(INFO) << "before initialization";
        std::exit(0);
      },
      testing::ExitedWithCode(0),
      "\033\\[33mWARNING\033\\[0m: Logging before "
      "\033\\[32mInitializeLogging\\(\\)\033\\[0m is written to "
      "\033\\[35mSTDERR\033\\[0m");
}

TEST(Logging, PreInitializationWarningHonorsColorFlag) {
  ASSERT_EQ(setenv("CLICOLOR_FORCE", "1", 1), 0);
  ASSERT_EQ(setenv("TERM", "xterm", 1), 0);
  unsetenv("NO_COLOR");

  ASSERT_EXIT(
      {
        FLAGS_colorlogtostderr = false;
        LOG(INFO) << "before initialization";
        std::exit(0);
      },
      testing::ExitedWithCode(0),
      "^WARNING: Logging before InitializeLogging\\(\\) is written to "
      "STDERR");
}
#endif

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
