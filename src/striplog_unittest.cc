// SPDX-FileCopyrightText: 2008-2023 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergey Ioffe

// The common part of the striplog tests.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iosfwd>
#include <string>

#include "base/commandlineflags.h"
#include "config.h"
#include "ng-log/logging.h"
#include "testing_utilities.h"

#ifndef NGLOG_STRIP_LOG
#  define NGLOG_STRIP_LOG 0
#endif

using namespace nglog;
using testing::HasSubstr;
using testing::Not;

namespace {
void handle_abort(int /*code*/) { std::exit(EXIT_FAILURE); }

constexpr char kInfoMessage[] = "strip info";
constexpr char kWarningMessage[] = "strip warning";
constexpr char kErrorMessage[] = "strip error";
}  // namespace

TEST(StripLog, LogsExpectedSeverities) {
  CaptureTestStderr();
  LOG(INFO) << kInfoMessage;
  LOG(WARNING) << kWarningMessage;
  LOG(ERROR) << kErrorMessage;
  const std::string output = GetCapturedTestStderr();

#if NGLOG_STRIP_LOG == 0
  EXPECT_THAT(output, HasSubstr(kInfoMessage));
  EXPECT_THAT(output, HasSubstr(kWarningMessage));
  EXPECT_THAT(output, HasSubstr(kErrorMessage));
#elif NGLOG_STRIP_LOG == 2
  EXPECT_THAT(output, Not(HasSubstr(kInfoMessage)));
  EXPECT_THAT(output, Not(HasSubstr(kWarningMessage)));
  EXPECT_THAT(output, HasSubstr(kErrorMessage));
#elif NGLOG_STRIP_LOG == 10
  EXPECT_THAT(output, Not(HasSubstr(kInfoMessage)));
  EXPECT_THAT(output, Not(HasSubstr(kWarningMessage)));
  EXPECT_THAT(output, Not(HasSubstr(kErrorMessage)));
#else
#  error Unsupported NGLOG_STRIP_LOG value
#endif
}

#ifdef GTEST_HAS_DEATH_TEST
TEST(StripLog, FatalIsNotStripped) {
#  if NGLOG_STRIP_LOG <= 3
  ASSERT_DEATH(LOG(FATAL) << "fatal log", "fatal log");
#  else
  ASSERT_DEATH(LOG(FATAL) << "fatal log", "");
#  endif
}
#endif

int main(int argc, char* argv[]) {
#if defined(_MSC_VER)
  // Avoid presenting an interactive dialog that will cause the test to time
  // out.
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif  // defined(_MSC_VER)
  std::signal(SIGABRT, handle_abort);

  FLAGS_logtostderr = true;
  InitializeLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);
  testing::InitGoogleMock(&argc, argv);
#if !DCHECK_IS_ON()
  DLOG(INFO) << "no debug output";
  DLOG_IF(WARNING, true) << "no conditional debug output";
  DVLOG(1) << "no verbose debug output";
#endif
  return RUN_ALL_TESTS();
}
