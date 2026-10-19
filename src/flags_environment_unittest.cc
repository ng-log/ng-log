// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "ng-log/flags.h"

TEST(FlagsEnvironment, PrefixedNameSetsTheDefault) {
  ASSERT_NE(nullptr, std::getenv("NGLOG_logtostderr"));
  EXPECT_TRUE(nglog::GetFlag(FLAGS_logtostderr));
}

TEST(FlagsEnvironment, BooleanValuesAreCaseInsensitive) {
  ASSERT_NE(nullptr, std::getenv("NGLOG_log_prefix"));
  EXPECT_FALSE(nglog::GetFlag(FLAGS_log_prefix));
}

// Flags that are on and off by default together tell a fallback to the
// default apart from a value that is parsed as either.
TEST(FlagsEnvironment, InvalidBooleanValuesUseTheDeclaredDefault) {
  ASSERT_NE(nullptr, std::getenv("NGLOG_log_year_in_prefix"));
  ASSERT_NE(nullptr, std::getenv("NGLOG_alsologtostderr"));
  EXPECT_TRUE(nglog::GetFlag(FLAGS_log_year_in_prefix));
  EXPECT_FALSE(nglog::GetFlag(FLAGS_alsologtostderr));
}

TEST(FlagsEnvironment, IntegersUseTheCommandLineSyntax) {
  ASSERT_NE(nullptr, std::getenv("NGLOG_v"));
  EXPECT_EQ(16, nglog::GetFlag(FLAGS_v));
}

TEST(FlagsEnvironment, FileModesUseOctalNotation) {
  ASSERT_NE(nullptr, std::getenv("NGLOG_logfile_mode"));
  EXPECT_EQ(0600U, nglog::GetFlag(FLAGS_logfile_mode).value());
}

TEST(FlagsEnvironment, StringValuesAreTakenVerbatim) {
  ASSERT_NE(nullptr, std::getenv("NGLOG_symbolize_file_base_path"));
  EXPECT_EQ("/opt/source tree", nglog::GetFlag(FLAGS_symbolize_file_base_path));
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
