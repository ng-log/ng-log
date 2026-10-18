// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Verifies that the experimental logging functions honor NGLOG_STRIP_LOG of
// the calling translation unit like the logging macros do.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <csignal>
#include <cstdio>  // IWYU pragma: keep
#include <cstdlib>
#include <string>

#include "ng-log/experimental/attributes.h"
// Checks are only exercised in release mode.
#include "ng-log/experimental/ensure.h"  // IWYU pragma: keep
// Enables the regular destinations of Log().
#include "ng-log/experimental/log.h"  // IWYU pragma: keep
#include "ng-log/experimental/signal_safe_log.h"
#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "testing_utilities.h"

namespace {

namespace ngx = nglog::experimental;

using testing::HasSubstr;
using testing::Not;

void HandleAbort(int) { std::exit(EXIT_FAILURE); }

constexpr char kInfoMessage[] = "strip info";
constexpr char kWarningMessage[] = "strip warning";
constexpr char kErrorMessage[] = "strip error";

TEST(ExperimentalStripLog, LogsExpectedSeverities) {
  nglog::ScopedStreamCapture capture{nglog::StandardStream::kStderr};
  ngx::Log(ngx::Info, kInfoMessage);
  ngx::Log(ngx::Warning, kWarningMessage);
  ngx::Log(ngx::Error, kErrorMessage);
  const std::string output = capture.Stop();

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

TEST(ExperimentalStripLog, SignalSafeLogsExpectedSeverities) {
  nglog::ScopedStreamCapture capture{nglog::StandardStream::kStderr};
  ngx::Log(ngx::SignalSafe | ngx::Info, kInfoMessage);
  ngx::Log(ngx::SignalSafe | ngx::Warning, kWarningMessage);
  ngx::Log(ngx::SignalSafe | ngx::Error, kErrorMessage);
  const std::string output = capture.Stop();

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

TEST(ExperimentalStripLog, DebugOnlyFollowsDebugMode) {
#if NGLOG_STRIP_LOG > 2
  GTEST_SKIP() << "Error messages are stripped regardless of debug mode";
#else
  nglog::ScopedStreamCapture capture{nglog::StandardStream::kStderr};
  ngx::Log(ngx::Error | ngx::Debug, kErrorMessage);
  const std::string output = capture.Stop();

#  if DCHECK_IS_ON()
  EXPECT_THAT(output, HasSubstr(kErrorMessage));
#  else
  EXPECT_THAT(output, Not(HasSubstr(kErrorMessage)));
#  endif
#endif
}

#ifdef GTEST_HAS_DEATH_TEST
#  if NGLOG_STRIP_LOG > 3
void ReportExitHandler() { std::fputs("exit handler ran", stderr); }
#  endif

// Like RAW_LOG(FATAL), a stripped fatal raw message still terminates. A signal
// handler may terminate only through functions that are async-signal-safe,
// which excludes exit handlers and static destructors.
TEST(ExperimentalStripLog, SignalSafeFatalIsNotStripped) {
#  if NGLOG_STRIP_LOG <= 3
  EXPECT_DEATH(ngx::Log(ngx::SignalSafe | ngx::Fatal, "raw fatal log"),
               "raw fatal log");
#  else
  EXPECT_EXIT(
      {
        std::atexit(&ReportExitHandler);
        ngx::Log(ngx::SignalSafe | ngx::Fatal, "raw fatal log");
      },
      testing::ExitedWithCode(EXIT_FAILURE), Not(HasSubstr("exit handler")));
#  endif
}

TEST(ExperimentalStripLog, FatalIsNotStripped) {
#  if NGLOG_STRIP_LOG <= 3
  EXPECT_DEATH(ngx::Log(ngx::Fatal, "fatal log"), "fatal log");
#  else
  EXPECT_DEATH(ngx::Log(ngx::Fatal, "fatal log"), "");
#  endif
}

// Like DLOG(FATAL), RAW_DLOG(FATAL) and DCHECK(), debug only messages are
// discarded in release mode even if their severity is stripped.
TEST(ExperimentalStripLog, DebugOnlyFatalDoesNotTerminateInReleaseMode) {
#  if DCHECK_IS_ON()
  GTEST_SKIP() << "debug only messages are logged in debug mode";
#  else
  EXPECT_EXIT(
      {
        ngx::Log(ngx::Fatal | ngx::Debug, "debug fatal log");
        ngx::Log(ngx::SignalSafe | ngx::Fatal | ngx::Debug,
                 "debug raw fatal log");
        ngx::Ensure(false, ngx::Debug, "debug check");
        std::exit(EXIT_SUCCESS);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
#  endif
}
#endif

}  // namespace

int main(int argc, char* argv[]) {
#if defined(_MSC_VER)
  // Avoid presenting an interactive dialog that will cause the test to time
  // out.
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif  // defined(_MSC_VER)
  std::signal(SIGABRT, HandleAbort);

  nglog::SetFlag(FLAGS_logtostderr, true);
  nglog::InitializeLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
