// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Verifies that Log() produces the output of the logging macros and that
// IsEnabled() behaves like VLOG_IS_ON().

#include "ng-log/experimental/log.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <initializer_list>
#include <ostream>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "ng-log/experimental/attributes.h"
#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "testing.h"
#include "testing_utilities.h"

namespace {

using namespace ngx_testing;
using testing::ElementsAre;
using testing::HasSubstr;
using testing::IsEmpty;
using testing::Not;

// Small thread IDs, such as on Windows, are padded with spaces. Others, such
// as on macOS, are printed in hexadecimal.
TEST(ExperimentalTesting, MaskTimestampsAcceptsPlatformThreadIds) {
  EXPECT_EQ(MaskTimestamps("W20261019 00:54:41.709787     1 file.cc:136] x\n"),
            "W<timestamp>     1 file.cc:<line>] x\n");
  EXPECT_EQ(MaskTimestamps("I20261019 00:54:41.709787 1234567 file.cc:7] y\n"),
            "I<timestamp> 1234567 file.cc:<line>] y\n");
  EXPECT_EQ(
      MaskTimestamps("W20261018 23:14:17.836564 0x1f66ee2c0 file.cc:137] z\n"),
      "W<timestamp> 0x1f66ee2c0 file.cc:<line>] z\n");
}

TEST(ExperimentalTesting, MaskThreadIdIgnoresProcess) {
  EXPECT_EQ(MaskThreadId("F<timestamp>  9928 file.cc:<line>] x"),
            MaskThreadId("F<timestamp>  9976 file.cc:<line>] x"));
  EXPECT_EQ(MaskThreadId("F<timestamp> 0x1f66ee2c0 file.cc:<line>] x"),
            "F<timestamp> <thread> file.cc:<line>] x");
}

// Clobbers errno while deciding whether to log, such as by querying the file
// system, and rejects the message.
struct ClobberingAdmission {
  bool Admit(ngx::Context& /*context*/) const {
    errno = ENOENT;
    return false;
  }
};

struct ClobberingThrottle {
  bool Throttle(ngx::Context& /*context*/) const {
    errno = ENOENT;
    return false;
  }
};

// A message that is not logged leaves errno as the caller had it.
TEST_F(ExperimentalLogging, RejectedMessageKeepsErrno) {
  errno = EACCES;
  ngx::Log(ngx::Attributes{ClobberingAdmission{}}, "rejected");
  EXPECT_EQ(errno, EACCES);
  ngx::Log(ngx::Attributes{ClobberingThrottle{}}, "throttled");
  EXPECT_EQ(errno, EACCES);
}

TEST_F(ExperimentalLogging, ReportsLocationOfCall) {
  RecordingSink sink;
  const int line = __LINE__ + 1;
  ngx::Log(ngx::Info | ngx::ToSink{&sink} | ngx::SkipLogFile, "located");
  EXPECT_THAT(sink.lines, ElementsAre(line));
  EXPECT_THAT(
      sink.messages,
      ElementsAre(std::format(
          "{}|{}|log_unittest.cc|located",
          static_cast<std::underlying_type_t<nglog::LogSeverity>>(NGLOG_INFO),
          __FILE__)));
}

TEST_F(ExperimentalLogging, InfoMatchesMacro) {
  EXPECT_SAME_OUTPUT(LOG(INFO) << "value " << 42,
                     ngx::Log(ngx::Info, "value {}", 42));
}

TEST_F(ExperimentalLogging, SeverityDefaultsToInfo) {
  EXPECT_SAME_OUTPUT(LOG(INFO) << "plain", ngx::Log("plain"));
}

TEST_F(ExperimentalLogging, WarningMatchesMacro) {
  EXPECT_SAME_OUTPUT(LOG(WARNING) << "careful",
                     ngx::Log(ngx::Warning, "careful"));
}

TEST_F(ExperimentalLogging, ErrorMatchesMacro) {
  EXPECT_SAME_OUTPUT(LOG(ERROR) << "broken " << 1.5,
                     ngx::Log(ngx::Error, "broken {}", 1.5));
}

TEST_F(ExperimentalLogging, RuntimeSeverityMatchesMacro) {
  const nglog::LogSeverity severity = NGLOG_WARNING;
  EXPECT_SAME_OUTPUT(LOG_AT_LEVEL(severity) << "runtime",
                     ngx::Log(ngx::Severity{severity}, "runtime"));
}

#ifdef GTEST_HAS_DEATH_TEST
// A severity outside the defined ones fails like a check instead of indexing
// past the names of the severities.
TEST_F(ExperimentalLogging, SeverityOutOfRangeFails) {
  const auto below = static_cast<nglog::LogSeverity>(NGLOG_INFO - 1);
  const auto above = static_cast<nglog::LogSeverity>(NGLOG_FATAL + 1);
  EXPECT_DEATH(ngx::Log(ngx::Severity{below}, "below"),
               "Check failed: severity -1 is not within \\[0, 3\\]");
  EXPECT_DEATH(ngx::Log(ngx::Severity{above}, "above"),
               "Check failed: severity 4 is not within \\[0, 3\\]");
}
#endif

TEST_F(ExperimentalLogging, FatalMatchesMacro) {
  EXPECT_SAME_DEATH(LOG(FATAL) << "boom " << 7,
                    ngx::Log(ngx::Fatal, "boom {}", 7));
}

TEST_F(ExperimentalLogging, FatalPropagatesFailureFunctionException) {
  const ScopedThrowOnFatal throw_on_fatal;
  const nglog::ScopedStreamCapture capture{nglog::StandardStream::kStderr};
  EXPECT_THROW(ngx::Log(ngx::Fatal, "boom"), std::logic_error);
}

TEST_F(ExperimentalLogging, FatalInDebugMatchesDebugFatalMacro) {
  const ScopedExitOnDFatal exit_on_dfatal{false};
  EXPECT_SAME_OUTPUT(LOG(DFATAL) << "debug fatal",
                     ngx::Log(ngx::Error | ngx::FatalInDebug, "debug fatal"));
  EXPECT_SAME_OUTPUT(LOG(DFATAL) << "debug fatal",
                     ngx::Log(ngx::FatalInDebug | ngx::Error, "debug fatal"));
}

// A C string argument does not turn the format string into a condition.
TEST_F(ExperimentalLogging, CStringArgumentMatchesMacro) {
  const char* const name = "pointer";
  EXPECT_SAME_OUTPUT(LOG(INFO) << "Hello " << "world",
                     ngx::Log("Hello {}", "world"));
  EXPECT_SAME_OUTPUT(LOG(INFO) << "Hello " << name, ngx::Log("Hello {}", name));
}

// A condition logs like LOG_IF(), although an if statement is preferred.
TEST_F(ExperimentalLogging, ConditionMatchesMacro) {
  for (const int value : {0, 1}) {
    const bool condition = value != 0;
    EXPECT_SAME_OUTPUT(LOG_IF(ERROR, condition) << "if " << value,
                       ngx::Log(condition, ngx::Error, "if {}", value));
    EXPECT_SAME_OUTPUT(LOG_IF(INFO, condition) << "if " << value,
                       ngx::Log(condition, "if {}", value));
  }
}

// Like LOG_IF_EVERY_N(), occurrences count only while the condition holds.
TEST_F(ExperimentalLogging, ConditionGatesRateLimits) {
  for (int i = 0; i != kRepetitions * 2; ++i) {
    const bool condition = i % 2 == 0;
    EXPECT_SAME_OUTPUT(LOG_IF_EVERY_N(INFO, condition, kEvery) << "every " << i,
                       ngx::Log(condition, ngx::Every{kEvery}, "every {}", i));
  }
}

#ifdef GTEST_HAS_DEATH_TEST
// Escalation leaves messages that are fatal already fatal, even if DFATAL
// messages do not terminate.
TEST_F(ExperimentalLogging, FatalInDebugKeepsFatalMessagesFatal) {
  const ScopedExitOnDFatal exit_on_dfatal{false};
  EXPECT_DEATH(ngx::Log(ngx::Fatal | ngx::FatalInDebug, "must terminate"),
               "must terminate");
}
#endif

TEST_F(ExperimentalLogging, LazyArgumentMatchesMacro) {
  const auto value = [] { return 42; };
  EXPECT_SAME_OUTPUT(LOG(INFO) << "lazy " << value(),
                     ngx::Log("lazy {}", ngx::Lazy(value)));
}

TEST_F(ExperimentalLogging, LazyArgumentHonorsFormatSpec) {
  const Output output =
      Capture([] { ngx::Log("[{:>5}]", ngx::Lazy([] { return 42; })); });
  EXPECT_THAT(output.stderr_text, HasSubstr("[   42]"));
}

TEST_F(ExperimentalLogging, LazyArgumentOfSuppressedMessageIsNotEvaluated) {
  nglog::SetFlag(
      FLAGS_minloglevel,
      static_cast<std::underlying_type_t<nglog::LogSeverity>>(NGLOG_ERROR));
  int evaluations = 0;
  const auto count = [&evaluations] { return ++evaluations; };
  const Output output =
      Capture([&count] { ngx::Log(ngx::Warning, "{}", ngx::Lazy(count)); });
  EXPECT_THAT(output.stderr_text, IsEmpty());
  EXPECT_EQ(evaluations, 0);
}

TEST_F(ExperimentalLogging, LazyArgumentOfRejectedMessageIsNotEvaluated) {
  int evaluations = 0;
  const auto count = [&evaluations] { return ++evaluations; };
  const Output output =
      Capture([&count] { ngx::Log(ngx::Verbose{1}, "{}", ngx::Lazy(count)); });
  EXPECT_THAT(output.stderr_text, IsEmpty());
  EXPECT_EQ(evaluations, 0);
}

TEST_F(ExperimentalLogging, LazyArgumentOfThrottledMessageIsNotEvaluated) {
  int evaluations = 0;
  const auto count = [&evaluations] { return ++evaluations; };
  for (int i = 0; i != kRepetitions; ++i) {
    Capture([&count] { ngx::Log(ngx::First{1}, "{}", ngx::Lazy(count)); });
  }
  EXPECT_EQ(evaluations, 1);
}

// The callable runs for each replacement field that refers to it.
TEST_F(ExperimentalLogging, LazyArgumentRunsPerReplacementField) {
  int evaluations = 0;
  const auto count = [&evaluations] { return ++evaluations; };
  const Output output =
      Capture([&count] { ngx::Log("{0} {0}", ngx::Lazy(count)); });
  EXPECT_THAT(output.stderr_text, HasSubstr("1 2"));
  EXPECT_EQ(evaluations, 2);
}

template <class F>
concept LazyFormattable = requires(F f) { ngx::Log("{}", ngx::Lazy(f)); };

// The result of the callable must be formattable.
static_assert(LazyFormattable<int (*)()>,
              "Lazy accepts a callable int (*)() with a formattable result");
static_assert(
    !LazyFormattable<void (*)()>,
    "Lazy rejects a callable void (*)() without a formattable result");
static_assert(
    !LazyFormattable<Unformattable (*)()>,
    "Lazy rejects a callable Unformattable (*)() without a formattable result");

TEST_F(ExperimentalLogging, IsEnabledMatchesVerboseMacro) {
  nglog::SetFlag(FLAGS_v, static_cast<std::int32_t>(1));
  EXPECT_TRUE(ngx::IsEnabled(ngx::Verbose{1}));
  EXPECT_EQ(ngx::IsEnabled(ngx::Verbose{1}), VLOG_IS_ON(1));
  EXPECT_FALSE(ngx::IsEnabled(ngx::Verbose{2}));
  EXPECT_EQ(ngx::IsEnabled(ngx::Verbose{2}), VLOG_IS_ON(2));
}

#ifdef GTEST_HAS_DEATH_TEST
TEST_F(ExperimentalLogging, IsEnabledHonorsModuleLevel) {
  EXPECT_EXIT(
      {
        nglog::SetVLOGLevel("log_unittest", 3);
        const bool enabled = ngx::IsEnabled(ngx::Verbose{3});
        const bool matches_macro =
            !kMacrosHonorModuleLevels || enabled == VLOG_IS_ON(3);
        std::exit(enabled && matches_macro ? EXIT_SUCCESS : EXIT_FAILURE);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
}
#endif

TEST_F(ExperimentalLogging, IsEnabledIgnoresMinimumLevelLikeMacro) {
  nglog::SetFlag(FLAGS_v, static_cast<std::int32_t>(1));
  nglog::SetFlag(
      FLAGS_minloglevel,
      static_cast<std::underlying_type_t<nglog::LogSeverity>>(NGLOG_ERROR));
  EXPECT_TRUE(ngx::IsEnabled(ngx::Verbose{1}));
  EXPECT_EQ(ngx::IsEnabled(ngx::Verbose{1}), VLOG_IS_ON(1));
}

TEST_F(ExperimentalLogging, ConstexprAttributesAreReusable) {
  nglog::SetFlag(FLAGS_v, static_cast<std::int32_t>(2));
  constexpr auto kStatistics = ngx::Verbose{2} | ngx::Tag{"stats"};
  ASSERT_TRUE(ngx::IsEnabled(kStatistics));
  EXPECT_SAME_OUTPUT(VLOG(2) << "statistics " << 3,
                     ngx::Log(kStatistics, "statistics {}", 3));
}

TEST_F(ExperimentalLogging, IsEnabledFollowsDebugMode) {
  EXPECT_EQ(ngx::IsEnabled(ngx::Debug), static_cast<bool>(DCHECK_IS_ON()));
}

template <class A>
concept Queryable =
    requires(const A& attributes) { ngx::IsEnabled(attributes); };

static_assert(Queryable<ngx::Verbose<>>, "IsEnabled accepts Verbose<>");
static_assert(Queryable<decltype(ngx::Verbose{1} | ngx::Debug)>,
              "IsEnabled accepts Verbose{1} | Debug");
// Querying must not count an occurrence that is never logged.
static_assert(!Queryable<ngx::Every<int>>,
              "IsEnabled rejects Every<int>, since evaluating its rate limit "
              "would count an occurrence");
static_assert(!Queryable<decltype(ngx::Verbose{1} | ngx::First{1})>,
              "IsEnabled rejects Verbose{1} | First{1}, since evaluating its "
              "rate limit would count an occurrence");

}  // namespace
