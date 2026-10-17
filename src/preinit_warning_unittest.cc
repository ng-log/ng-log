// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include <gtest/gtest.h>

#include <cstdlib>

#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "testing_utilities.h"

using namespace nglog;

#if !defined(NGLOG_OS_WINDOWS) && defined(GTEST_HAS_DEATH_TEST)
namespace {

// Forces a color capable terminal so that only the color flag decides
// whether the warning is colored.
class PreInitializationWarning : public testing::Test {
 private:
  ScopedEnvironmentVariable clicolor_force_{"CLICOLOR_FORCE", "1"};
  ScopedEnvironmentVariable term_{"TERM", "xterm"};
  ScopedEnvironmentVariable no_color_{"NO_COLOR", nullptr};
};

}  // namespace

TEST_F(PreInitializationWarning, UsesFieldColors) {
  nglog::SetFlag(FLAGS_colorlogtostderr, true);
  EXPECT_EXIT(
      {
        LOG(INFO) << "before initialization";
        std::exit(0);
      },
      testing::ExitedWithCode(0),
      "\033\\[33mWARNING\033\\[0m: Logging before "
      "\033\\[32mInitializeLogging\\(\\)\033\\[0m is written to "
      "\033\\[35mSTDERR\033\\[0m");
}

TEST_F(PreInitializationWarning, HonorsColorFlag) {
  EXPECT_EXIT(
      {
        nglog::SetFlag(FLAGS_colorlogtostderr, false);
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
