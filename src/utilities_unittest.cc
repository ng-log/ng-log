// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Shinichiro Hamaji
#include "utilities.h"

#include <gtest/gtest.h>

#if defined(NGLOG_OS_WINDOWS) && defined(HAVE_GET_THREAD_DESCRIPTION)
#  include <windows.h>
#elif defined(HAVE_PTHREAD_GETNAME_NP)
#  include <pthread.h>
#endif

#include "internal/emscripten_console.h"
#include "ng-log/logging.h"
#include "testing_utilities.h"

#ifdef NGLOG_USE_GFLAGS
#  include <gflags/gflags.h>
using namespace GFLAGS_NAMESPACE;
#endif

using namespace nglog;

#ifdef GTEST_HAS_DEATH_TEST
TEST(utilities, InitializeLoggingDeathTest) {
  ASSERT_DEATH(InitializeLogging("foobar"), "");
}
#endif

TEST(utilities, MakeLogFilename) {
  EXPECT_EQ(nglog::MakeLogFilename("/tmp/app[1].", "20260817-123456.42",
                                   ".foo+", true),
            "/tmp/app[1].20260817-123456.42.foo+");
  EXPECT_EQ(nglog::MakeLogFilename("/tmp/app[1].", "20260817-123456.42",
                                   ".foo+", false),
            "/tmp/app[1]..foo+");
}

TEST(utilities, MakeLogFilenameMatcher) {
  const std::regex timestamp_regex =
      nglog::tools::MakeLogFilenameMatcher("app[1].", ".foo+", true);
  EXPECT_TRUE(
      std::regex_match("app[1].20260817-123456.42.foo+", timestamp_regex));
  EXPECT_FALSE(
      std::regex_match("app11.20260817-123456.42.foo+", timestamp_regex));

  const std::regex non_timestamp_regex =
      nglog::tools::MakeLogFilenameMatcher("app[1].", ".foo+", false);
  EXPECT_TRUE(std::regex_match("app[1]..foo+", non_timestamp_regex));
  EXPECT_FALSE(
      std::regex_match("app[1].20260817-123456.42.foo+", non_timestamp_regex));
}

TEST(utilities, MakeLogFilenameMatcherRequiresBaseFilename) {
  const std::regex regex =
      nglog::tools::MakeLogFilenameMatcher("", ".foo+", true);
  EXPECT_FALSE(std::regex_match("20260817-123456.42.foo+", regex));
}

TEST(EmscriptenConsole, MapsSeverityToConsoleLevel) {
  EXPECT_EQ(internal::EmscriptenLogLevelForSeverity(NGLOG_INFO),
            internal::EmscriptenLogLevel::kOut);
  EXPECT_EQ(internal::EmscriptenLogLevelForSeverity(NGLOG_WARNING),
            internal::EmscriptenLogLevel::kWarn);
  EXPECT_EQ(internal::EmscriptenLogLevelForSeverity(NGLOG_ERROR),
            internal::EmscriptenLogLevel::kError);
  EXPECT_EQ(internal::EmscriptenLogLevelForSeverity(NGLOG_FATAL),
            internal::EmscriptenLogLevel::kDbg);
}

TEST(utilities, TrimTrailingCRLFRemovesTrailingNewlines) {
  EXPECT_EQ(nglog::TrimTrailingCRLF("message\r\n"), "message");
  EXPECT_EQ(nglog::TrimTrailingCRLF("message\n"), "message");
  EXPECT_EQ(nglog::TrimTrailingCRLF("message"), "message");
  EXPECT_EQ(nglog::TrimTrailingCRLF("\r\n"), "");
}

TEST(utilities, TrimTrailingCharacters) {
  constexpr char delimiters[] = {' ', '\t'};

  EXPECT_EQ(nglog::TrimTrailingCharacters("message \t", delimiters), "message");
  EXPECT_EQ(nglog::TrimTrailingCharacters("message \t", delimiters,
                                          sizeof(delimiters)),
            "message");
  EXPECT_EQ(nglog::TrimTrailingCharacters("message", " \t"), "message");
  EXPECT_EQ(nglog::TrimTrailingCharacters(" \t", delimiters), "");
}

#if defined(NGLOG_OS_WINDOWS) && defined(HAVE_GET_THREAD_DESCRIPTION)
TEST(utilities, SetThreadName) {
  constexpr char kThreadName[] = "TestThread";
  nglog::SetThreadName(kThreadName);

  PWSTR thread_name = nullptr;
  ASSERT_TRUE(
      SUCCEEDED(GetThreadDescription(GetCurrentThread(), &thread_name)));
  ASSERT_NE(thread_name, nullptr);
  EXPECT_STREQ(thread_name, L"TestThread");
  LocalFree(thread_name);
}
#elif defined(HAVE_PTHREAD_GETNAME_NP)
TEST(utilities, SetThreadName) {
  constexpr char kThreadName[] = "TestThread";
  nglog::SetThreadName(kThreadName);

  char thread_name[16];
  ASSERT_EQ(
      pthread_getname_np(pthread_self(), thread_name, sizeof(thread_name)), 0);
  EXPECT_STREQ(thread_name, kThreadName);
}
#endif

int main(int argc, char** argv) {
  InitializeLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);

  CHECK_EQ(RUN_ALL_TESTS(), 0);
}
