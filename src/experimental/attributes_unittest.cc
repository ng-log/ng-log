// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Verifies the built-in and custom attributes and their composition.

#include "ng-log/experimental/attributes.h"

// Attributes are observed through the messages they shape.
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <compare>
#include <cstdarg>
// The C library headers define __GLIBC__ on glibc.
// IWYU pragma: no_include <features.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <initializer_list>
#include <latch>
#include <limits>
#include <ostream>
#include <ratio>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <version>

#include "ng-log/experimental/ensure.h"
#include "ng-log/experimental/format.h"
#include "ng-log/experimental/log.h"
#include "ng-log/experimental/signal_safe_log.h"
#include "ng-log/experimental/sink.h"
#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "testing.h"

// Clobbers errno while being formatted.
struct ErrnoClobbering {};

template <>
struct std::formatter<ErrnoClobbering> : std::formatter<std::string_view> {
  std::format_context::iterator format(ErrnoClobbering,
                                       std::format_context& context) const {
    errno = 0;
    return std::formatter<std::string_view>::format("file", context);
  }
};

namespace {

using namespace ngx_testing;
using testing::Each;
using testing::ElementsAre;
using testing::HasSubstr;
using testing::IsEmpty;
using testing::Not;

// Records the messages a structured sink receives.
class RecordingStructuredSink final : public ngx::Sink {
 public:
  struct Received {
    nglog::LogSeverity severity;
    std::string text;
    std::uint_least32_t line;
    std::string function;
    std::string tag;
    ngx::Record::Time time;
    std::vector<std::string> fields;
  };

  void Send(const ngx::Record& record) override {
    std::vector<std::string> fields;
    for (const ngx::Field& field : record.fields()) {
      fields.push_back(std::format("{}={}", field.name, field.value));
    }
    received.push_back(Received{record.severity(), std::string{record.text()},
                                record.location().line(),
                                record.location().function_name(),
                                record.tag() == nullptr ? "" : record.tag(),
                                record.time(), std::move(fields)});
  }

  std::vector<Received> received;
};

#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
// Records the zoned time of the last message a structured sink receives.
class ZoneSink final : public ngx::Sink {
 public:
  void Send(const ngx::Record& record) override {
    const auto zoned = record.zoned_time();
    zone = zoned.get_time_zone();
    offset = zoned.get_info().offset;
    utc_offset = record.utc_offset();
    utc = record.utc();
    same_time = zoned.get_sys_time() == record.time();
  }

  const std::chrono::time_zone* zone = nullptr;
  bool utc = false;
  std::chrono::seconds offset{};
  std::chrono::seconds utc_offset{};
  bool same_time = false;
};
#endif

// Adds a field to the message. A message may carry several of them.
struct Annotation {
  using Multiplicity = ngx::Multiple;

  std::string_view name;
  std::string_view value;

  void Fields(ngx::FieldWriter& fields) const { fields.Add(name, value); }
};

// Counts how often the fields of the message are collected.
struct CountedFields {
  int* count;

  void Fields(ngx::FieldWriter&) const { ++*count; }
};

// Sets errno to zero while being evaluated.
const char* ClobberErrno() {
  errno = 0;
  return "file";
}

// Source of the value Snapshot captures.
int g_snapshot_source = 0;

// Captures a value at the call site and appends it after the message.
struct Snapshot {
  int value = 0;

  Snapshot Capture() const { return Snapshot{g_snapshot_source}; }

  void Suffix(ngx::Message& message) const {
    std::format_to(message.out(), " [{}]", value);
  }
};

TEST_F(ExperimentalLogging, ErrnoMatchesMacro) {
  EXPECT_SAME_OUTPUT(
      (errno = ENOENT, PLOG(ERROR) << "open " << "file"),
      (errno = ENOENT, ngx::Log(ngx::Error | ngx::Errno, "open {}", "file")));
}

// Formatting runs after errno is preserved, so formatters and lazy arguments
// that clobber it do not change the reported error.
TEST_F(ExperimentalLogging, ErrnoIgnoresClobberingFormatters) {
  const auto clobber = [] {
    errno = 0;
    return "file";
  };
  EXPECT_SAME_OUTPUT((errno = ENOENT, PLOG(ERROR) << "open " << "file"),
                     (errno = ENOENT, ngx::Log(ngx::Error | ngx::Errno,
                                               "open {}", ngx::Lazy{clobber})));
  EXPECT_SAME_OUTPUT((errno = ENOENT, PLOG(ERROR) << "open " << "file"),
                     (errno = ENOENT, ngx::Log(ngx::Error | ngx::Errno,
                                               "open {}", ErrnoClobbering{})));
}

// Braces evaluate the attributes before the arguments, so errno is captured
// before an argument clobbers it like PLOG() does.
TEST_F(ExperimentalLogging, BracedErrnoCapturedBeforeArguments) {
  if (!kOrderedBracedArguments) {
    GTEST_SKIP() << "Braced arguments are evaluated out of order";
  }
  EXPECT_SAME_OUTPUT((errno = ENOENT, PLOG(ERROR) << "open " << ClobberErrno()),
                     (errno = ENOENT, ngx::Log{ngx::Error | ngx::Errno,
                                               "open {}", ClobberErrno()}));
  EXPECT_SAME_OUTPUT(
      (errno = EACCES, PLOG(INFO) << "open " << ClobberErrno()),
      (errno = EACCES, ngx::Log{ngx::Errno, "open {}", ClobberErrno()}));
}

// The logger of a two-stage call is created before the arguments of the
// second stage are evaluated, so errno is captured first.
TEST_F(ExperimentalLogging, TwoStageErrnoCapturedBeforeArguments) {
  EXPECT_SAME_OUTPUT((errno = ENOENT, PLOG(ERROR) << "open " << ClobberErrno()),
                     (errno = ENOENT, ngx::Log(ngx::Error | ngx::Errno)(
                                          "open {}", ClobberErrno())));
}

// User attributes may capture state at the call site as well.
TEST_F(ExperimentalLogging, BracedUserCaptureRunsBeforeArguments) {
  if (!kOrderedBracedArguments) {
    GTEST_SKIP() << "Braced arguments are evaluated out of order";
  }
  g_snapshot_source = 0;
  const Output braced = Capture(
      [] { ngx::Log{ngx::Info | Snapshot{}, "next {}", ++g_snapshot_source}; });
  EXPECT_THAT(braced.stderr_text, HasSubstr("next 1 [0]"));
  const Output two_stage = Capture(
      [] { ngx::Log(ngx::Info | Snapshot{})("next {}", ++g_snapshot_source); });
  EXPECT_THAT(two_stage.stderr_text, HasSubstr("next 2 [1]"));
}

#ifdef GTEST_HAS_DEATH_TEST
// Ensure() runs the Capture() hooks of its attributes as well.
TEST_F(ExperimentalLogging, EnsureRunsUserCapture) {
  g_snapshot_source = 5;
  EXPECT_DEATH(ngx::Ensure(false, Snapshot{}, "captured"), "captured \\[5\\]");
}
#endif

TEST_F(ExperimentalLogging, TagKeepsOutput) {
  EXPECT_SAME_OUTPUT(LOG(WARNING) << "tagged",
                     ngx::Log(ngx::Warning | ngx::Tag{"network"}, "tagged"));
}

TEST_F(ExperimentalLogging, DebugMatchesMacro) {
  EXPECT_SAME_OUTPUT(DLOG(INFO) << "debug " << 3,
                     ngx::Log(ngx::Info | ngx::Debug, "debug {}", 3));
}

TEST_F(ExperimentalLogging, VerboseEnabledMatchesMacro) {
  nglog::SetFlag(FLAGS_v, static_cast<std::int32_t>(2));
  EXPECT_SAME_OUTPUT(VLOG(2) << "verbose",
                     ngx::Log(ngx::Verbose{2}, "verbose"));
}

TEST_F(ExperimentalLogging, VerboseDisabledMatchesMacro) {
  nglog::SetFlag(FLAGS_v, static_cast<std::int32_t>(1));
  EXPECT_SAME_OUTPUT(VLOG(2) << "verbose",
                     ngx::Log(ngx::Verbose{2}, "verbose"));
}

#ifdef GTEST_HAS_DEATH_TEST
// A --vmodule pattern cannot be removed once set. Tests that set one run in a
// child process so that later tests keep being controlled by --v.
TEST_F(ExperimentalLogging, VerboseHonorsModuleLevel) {
  EXPECT_EXIT(
      {
        nglog::SetVLOGLevel("attributes_unittest", 3);
        if (kMacrosHonorModuleLevels) {
          EXPECT_SAME_OUTPUT(VLOG(3) << "module",
                             ngx::Log(ngx::Verbose{3}, "module"));
        }
        const Output output =
            Capture([] { ngx::Log(ngx::Verbose{3}, "module"); });
        std::exit(!testing::Test::HasFailure() &&
                          output.stderr_text.find("module") != std::string::npos
                      ? EXIT_SUCCESS
                      : EXIT_FAILURE);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
}
#endif

TEST_F(ExperimentalLogging, EveryCountMatchesMacro) {
  for (int i = 0; i != kRepetitions; ++i) {
    EXPECT_SAME_OUTPUT(LOG_EVERY_N(INFO, kEvery) << "every " << i,
                       ngx::Log(ngx::Info | ngx::Every{kEvery}, "every {}", i));
  }
}

TEST_F(ExperimentalLogging, FirstMatchesMacro) {
  for (int i = 0; i != kRepetitions; ++i) {
    EXPECT_SAME_OUTPUT(LOG_FIRST_N(INFO, kEvery) << "first " << i,
                       ngx::Log(ngx::Info | ngx::First{kEvery}, "first {}", i));
  }
}

TEST_F(ExperimentalLogging, EveryCountWithSizeMatchesMacro) {
  constexpr std::size_t kSizeEvery = kEvery;
  for (int i = 0; i != kRepetitions; ++i) {
    EXPECT_SAME_OUTPUT(
        LOG_EVERY_N(INFO, kEvery) << "every " << i,
        ngx::Log(ngx::Info | ngx::Every{kSizeEvery}, "every {}", i));
  }
}

TEST_F(ExperimentalLogging, FirstWithUnsignedMatchesMacro) {
  constexpr unsigned kUnsignedFirst = kEvery;
  for (int i = 0; i != kRepetitions; ++i) {
    EXPECT_SAME_OUTPUT(
        LOG_FIRST_N(INFO, kEvery) << "first " << i,
        ngx::Log(ngx::Info | ngx::First{kUnsignedFirst}, "first {}", i));
  }
}

TEST_F(ExperimentalLogging, VerboseWithLongMatchesMacro) {
  nglog::SetFlag(FLAGS_v, static_cast<std::int32_t>(2));
  constexpr long kLevel = 2;
  EXPECT_SAME_OUTPUT(VLOG(kLevel) << "long level",
                     ngx::Log(ngx::Verbose{kLevel}, "long level"));
}

// Levels outside the range of the verbosity flag saturate instead of wrapping
// around.
TEST_F(ExperimentalLogging, VerboseOutOfRangeLevelSaturates) {
  nglog::SetFlag(FLAGS_v, static_cast<std::int32_t>(1));
  EXPECT_FALSE(
      ngx::IsEnabled(ngx::Verbose{std::numeric_limits<long long>::max()}));
  EXPECT_FALSE(ngx::IsEnabled(
      ngx::Verbose{std::numeric_limits<unsigned long long>::max()}));
  EXPECT_TRUE(
      ngx::IsEnabled(ngx::Verbose{std::numeric_limits<long long>::min()}));
  // Truncated to 32 bits, the level would be 5 instead of the minimum.
  constexpr long long kWrapsToFive = -(1LL << 32) + 5;
  EXPECT_TRUE(ngx::IsEnabled(ngx::Verbose{kWrapsToFive}));
}

// Converts a period to the seconds LOG_EVERY_T() expects.
template <class Rep, class Period>
constexpr double Seconds(std::chrono::duration<Rep, Period> period) {
  return std::chrono::duration<double>{period}.count();
}

// Returns the indices of the iterations whose statement logged.
template <class Statement>
std::vector<int> LoggedIterations(int iterations, Statement statement) {
  std::vector<int> logged;
  for (int i = 0; i != iterations; ++i) {
    if (!Capture([&statement, i] { statement(i); }).stderr_text.empty()) {
      logged.push_back(i);
    }
  }
  return logged;
}

TEST_F(ExperimentalLogging, EveryCountAtCounterLimitDoesNotOverflow) {
  constexpr signed char kLimit = std::numeric_limits<signed char>::max();
  constexpr int kIterations = kLimit + 3;
  EXPECT_THAT(LoggedIterations(
                  kIterations,
                  [](int i) { ngx::Log(ngx::Every{kLimit}, "every {}", i); }),
              ElementsAre(0, kLimit));
}

TEST_F(ExperimentalLogging, FirstAtCounterLimitDoesNotOverflow) {
  constexpr unsigned char kLimit = std::numeric_limits<unsigned char>::max();
  constexpr int kIterations = kLimit + 3;
  EXPECT_EQ(LoggedIterations(
                kIterations,
                [](int i) { ngx::Log(ngx::First{kLimit}, "first {}", i); })
                .size(),
            std::size_t{kLimit});
}

TEST_F(ExperimentalLogging, EveryCountWithZeroMatchesMacro) {
  constexpr int kZero = 0;
  for (int i = 0; i != kRepetitions; ++i) {
    EXPECT_SAME_OUTPUT(LOG_EVERY_N(INFO, kZero) << "zero " << i,
                       ngx::Log(ngx::Every{kZero}, "zero {}", i));
  }
}

TEST_F(ExperimentalLogging, EveryCountWithNegativeMatchesMacro) {
  constexpr int kNegative = -2;
  for (int i = 0; i != kRepetitions; ++i) {
    EXPECT_SAME_OUTPUT(LOG_EVERY_N(INFO, kNegative) << "negative " << i,
                       ngx::Log(ngx::Every{kNegative}, "negative {}", i));
  }
}

TEST_F(ExperimentalLogging, FirstWithNegativeMatchesMacro) {
  constexpr int kNegative = -2;
  for (int i = 0; i != kRepetitions; ++i) {
    EXPECT_SAME_OUTPUT(LOG_FIRST_N(INFO, kNegative) << "negative " << i,
                       ngx::Log(ngx::First{kNegative}, "negative {}", i));
  }
}

// LOG_EVERY_T() logs once the steady clock exceeds the period since its epoch,
// so a period longer than the uptime never logs.
TEST_F(ExperimentalLogging, EveryPeriodLongerThanUptimeMatchesMacro) {
  using namespace std::chrono_literals;
  constexpr auto kCentury = 3.2e9s;
  for (int i = 0; i != kRepetitions; ++i) {
    EXPECT_SAME_OUTPUT(LOG_EVERY_T(INFO, Seconds(kCentury)) << "century " << i,
                       ngx::Log(ngx::Every{kCentury}, "century {}", i));
  }
}

// Periods beyond the range of nanoseconds saturate instead of overflowing,
// which the undefined behavior sanitizer would report.
TEST_F(ExperimentalLogging, EveryPeriodWithHugePeriodDoesNotOverflow) {
  using namespace std::chrono_literals;
  // The largest float count below the range of nanoseconds, which rounds up
  // to it when converted to nanoseconds in float. Floating-point chrono
  // literals cannot express it, since their representation is long double.
  constexpr std::chrono::duration<float> kFloatBelowLimit{9223371776.0F};
  EXPECT_THAT(LoggedIterations(kRepetitions,
                               [kFloatBelowLimit](int i) {
                                 ngx::Log(ngx::Every{kFloatBelowLimit},
                                          "float below limit {}", i);
                               }),
              IsEmpty());
  EXPECT_THAT(LoggedIterations(
                  kRepetitions,
                  [](int i) { ngx::Log(ngx::Every{1e300s}, "literal {}", i); }),
              IsEmpty());
  EXPECT_THAT(LoggedIterations(kRepetitions,
                               [](int i) {
                                 ngx::Log(ngx::Every{std::chrono::years{1000}},
                                          "years {}", i);
                               }),
              IsEmpty());
  EXPECT_THAT(LoggedIterations(kRepetitions,
                               [](int i) {
                                 ngx::Log(
                                     ngx::Every{std::chrono::duration<double>{
                                         std::numeric_limits<double>::max()}},
                                     "double {}", i);
                               }),
              IsEmpty());
  EXPECT_THAT(LoggedIterations(
                  kRepetitions,
                  [](int i) {
                    ngx::Log(ngx::Every{std::chrono::duration<double>{
                                 std::numeric_limits<double>::quiet_NaN()}},
                             "nan {}", i);
                  }),
              IsEmpty());
  EXPECT_THAT(LoggedIterations(kRepetitions,
                               [](int i) {
                                 ngx::Log(ngx::Every{std::chrono::years{-1000}},
                                          "negative {}", i);
                               }),
              ElementsAre(0, 1, 2, 3, 4, 5, 6));
}

// Periods within the range of nanoseconds convert exactly like
// std::chrono::duration_cast, also if multiplying the count first, as
// duration_cast does, would overflow.
TEST(ExperimentalAttributes, PeriodInRangeConvertsExactly) {
  using ngx::internal::SaturatingNanoseconds;
  using std::chrono::duration;
  using std::chrono::duration_cast;
  using std::chrono::nanoseconds;
  constexpr auto kMaxCount = std::numeric_limits<long long>::max();
  constexpr auto kMaxUnsignedCount =
      std::numeric_limits<unsigned long long>::max();
  const duration<long long, std::pico> picoseconds{kMaxCount};
  const duration<unsigned long long, std::pico> unsigned_picoseconds{
      kMaxUnsignedCount};
  EXPECT_EQ(SaturatingNanoseconds(picoseconds),
            duration_cast<nanoseconds>(picoseconds));
  EXPECT_EQ(SaturatingNanoseconds(unsigned_picoseconds),
            duration_cast<nanoseconds>(unsigned_picoseconds));
  // Thirds of a second whose count exceeds the range of nanoseconds once
  // multiplied by the 10^9 nanoseconds of a second, unlike the result.
  constexpr long long kThirds = 20'000'000'000;
  constexpr long long kExpectedNanoseconds = 6'666'666'666'666'666'666;
  EXPECT_EQ(
      SaturatingNanoseconds(duration<long long, std::ratio<1, 3>>{kThirds}),
      nanoseconds{kExpectedNanoseconds});
  EXPECT_EQ(
      SaturatingNanoseconds(duration<long long, std::ratio<1, 3>>{-kThirds}),
      nanoseconds{-kExpectedNanoseconds});
  // Floating-point periods keep their fraction of a second.
  using namespace std::chrono_literals;
  constexpr nanoseconds kHalfSecond{500'000'000};
  constexpr nanoseconds kHourAndHalfSecond{3'600'500'000'000};
  EXPECT_EQ(SaturatingNanoseconds(0.5s), kHalfSecond);
  EXPECT_EQ(SaturatingNanoseconds(3600.5s), kHourAndHalfSecond);
}

// Periods beyond the range of nanoseconds saturate in integer arithmetic,
// which, unlike long double, cannot round a product below the limit.
TEST(ExperimentalAttributes, PeriodBeyondRangeSaturates) {
  using ngx::internal::SaturatingNanoseconds;
  using std::chrono::duration;
  using std::chrono::nanoseconds;
  // Ticks of 103 nanoseconds whose product with the count is at least 2^63
  // but rounds below it in a 64-bit long double.
  constexpr long long kTicks = 89'547'301'328'687'144;
  using Ticks = duration<long long, std::ratio<103, 1'000'000'000>>;
  EXPECT_EQ(SaturatingNanoseconds(Ticks{kTicks}), nanoseconds::max());
  EXPECT_EQ(SaturatingNanoseconds(Ticks{-kTicks}), nanoseconds::min());
}

// LOG_EVERY_T() logs once the steady clock exceeds the period since its epoch,
// so on a host that just started neither side logs.
TEST_F(ExperimentalLogging, EveryPeriodMatchesMacro) {
  using namespace std::chrono_literals;
  constexpr auto kPeriod = 1min;
  if (std::chrono::steady_clock::now().time_since_epoch() <= kPeriod) {
    GTEST_SKIP() << "The steady clock has not exceeded the period yet";
  }
  for (int i = 0; i != kRepetitions; ++i) {
    EXPECT_SAME_OUTPUT(
        LOG_EVERY_T(INFO, Seconds(kPeriod)) << "timed " << i,
        ngx::Log(ngx::Info | ngx::Every{kPeriod}, "timed {}", i));
  }
}

// Half the current time since the epoch of the steady clock is exceeded
// already, so the first message is logged on any host and the others are
// suppressed.
TEST_F(ExperimentalLogging, EveryPeriodLogsFirstMessage) {
  const auto period = std::chrono::steady_clock::now().time_since_epoch() / 2;
  EXPECT_THAT(LoggedIterations(kRepetitions,
                               [period](int i) {
                                 ngx::Log(ngx::Every{period}, "timed {}", i);
                               }),
              ElementsAre(0));
}

TEST_F(ExperimentalLogging, OccurrenceOfEveryCountMatchesMacro) {
  for (int i = 0; i != kRepetitions; ++i) {
    EXPECT_SAME_OUTPUT(
        LOG_EVERY_N(INFO, kEvery) << "occurrence " << LOG_OCCURRENCES,
        ngx::Log(ngx::Every{kEvery}, "occurrence {}", ngx::Occurrence));
  }
}

TEST_F(ExperimentalLogging, OccurrenceOfFirstMatchesMacro) {
  for (int i = 0; i != kRepetitions; ++i) {
    EXPECT_SAME_OUTPUT(
        LOG_FIRST_N(INFO, kEvery) << "occurrence " << LOG_OCCURRENCES,
        ngx::Log(ngx::First{kEvery}, "occurrence {}", ngx::Occurrence));
  }
}

// The type of the counter is pinned by the wraparound at its limit below.
TEST_F(ExperimentalLogging, OccurrenceWithSizeCounterMatchesMacro) {
  constexpr std::size_t kSizeEvery = kEvery;
  for (int i = 0; i != kRepetitions; ++i) {
    EXPECT_SAME_OUTPUT(
        LOG_EVERY_N(INFO, kEvery) << "size " << LOG_OCCURRENCES,
        ngx::Log(ngx::Every{kSizeEvery}, "size {}", ngx::Occurrence));
  }
}

// Occurrences wrap around at the limit of the counter type without undefined
// behavior.
TEST_F(ExperimentalLogging, OccurrenceWrapsAroundAtCounterLimit) {
  constexpr signed char kEveryOccurrence = 1;
  constexpr std::size_t kOccurrences =
      std::numeric_limits<signed char>::max() + 2;
  std::vector<std::string> texts;
  for (std::size_t i = 0; i != kOccurrences; ++i) {
    ngx::Log(ngx::Every{kEveryOccurrence} | ngx::ToVector{&texts}, "{}",
             ngx::Occurrence);
  }
  ASSERT_EQ(texts.size(), kOccurrences);
  EXPECT_EQ(texts[kOccurrences - 3], "127");
  EXPECT_EQ(texts[kOccurrences - 2], "-128");
  EXPECT_EQ(texts[kOccurrences - 1], "-127");
}

TEST_F(ExperimentalLogging, VerboseEveryCountMatchesMacro) {
  nglog::SetFlag(FLAGS_v, static_cast<std::int32_t>(1));
  for (int i = 0; i != kRepetitions; ++i) {
    EXPECT_SAME_OUTPUT(
        VLOG_EVERY_N(1, kEvery) << "verbose every " << i,
        ngx::Log(ngx::Verbose{1} | ngx::Every{kEvery}, "verbose every {}", i));
  }
}

TEST_F(ExperimentalLogging, RateLimitCountsSuppressedSeverity) {
  for (int i = 0; i != kRepetitions; ++i) {
    nglog::SetFlag(
        FLAGS_minloglevel,
        static_cast<std::int32_t>(i % 2 == 0 ? NGLOG_ERROR : NGLOG_INFO));
    EXPECT_SAME_OUTPUT(
        LOG_EVERY_N(INFO, kEvery) << "parity " << i,
        ngx::Log(ngx::Info | ngx::Every{kEvery}, "parity {}", i));
  }
}

TEST_F(ExperimentalLogging, SinkMatchesMacro) {
  RecordingSink expected_sink;
  RecordingSink actual_sink;
  EXPECT_SAME_OUTPUT(
      LOG_TO_SINK(&expected_sink, WARNING) << "to sink",
      ngx::Log(ngx::Warning | ngx::ToSink{&actual_sink}, "to sink"));
  EXPECT_EQ(actual_sink.messages, expected_sink.messages);
  EXPECT_THAT(actual_sink.messages, Not(IsEmpty()));
}

TEST_F(ExperimentalLogging, SinkWithoutLogFileMatchesMacro) {
  RecordingSink expected_sink;
  RecordingSink actual_sink;
  EXPECT_SAME_OUTPUT(
      LOG_TO_SINK_BUT_NOT_TO_LOGFILE(&expected_sink, WARNING) << "only sink",
      ngx::Log(ngx::Warning | ngx::ToSink{&actual_sink} | ngx::SkipLogFile,
               "only sink"));
  EXPECT_EQ(actual_sink.messages, expected_sink.messages);
  EXPECT_THAT(actual_sink.messages, Not(IsEmpty()));
}

// Each of several sinks receives the message as a single sink does.
TEST_F(ExperimentalLogging, RepeatedSinksEachReceiveMessage) {
  RecordingSink expected_sink;
  RecordingSink first_sink;
  RecordingSink second_sink;
  EXPECT_SAME_OUTPUT(LOG_TO_SINK(&expected_sink, WARNING) << "to sinks",
                     ngx::Log(ngx::Warning | ngx::ToSink{&first_sink} |
                                  ngx::ToSink{&second_sink},
                              "to sinks"));
  EXPECT_THAT(first_sink.messages, Not(IsEmpty()));
  EXPECT_EQ(first_sink.messages, expected_sink.messages);
  EXPECT_EQ(second_sink.messages, expected_sink.messages);
}

TEST_F(ExperimentalLogging, RepeatedSinksWithoutLogFileEachReceiveMessage) {
  RecordingSink expected_sink;
  RecordingSink first_sink;
  RecordingSink second_sink;
  EXPECT_SAME_OUTPUT(LOG_TO_SINK_BUT_NOT_TO_LOGFILE(&expected_sink, WARNING)
                         << "only sinks",
                     ngx::Log(ngx::Warning | ngx::ToSink{&first_sink} |
                                  ngx::ToSink{&second_sink} | ngx::SkipLogFile,
                              "only sinks"));
  EXPECT_THAT(first_sink.messages, Not(IsEmpty()));
  EXPECT_EQ(first_sink.messages, expected_sink.messages);
  EXPECT_EQ(second_sink.messages, expected_sink.messages);
}

// A structured sink receives the message with its location, tag, time and
// the fields of its attributes in composition order.
TEST_F(ExperimentalLogging, StructuredSinkReceivesRecord) {
  RecordingStructuredSink sink;
  RecordingSink legacy_sink;
  const auto before = std::chrono::system_clock::now();
  const Output output = Capture([&sink, &legacy_sink] {
    ngx::Log(ngx::Warning | ngx::Tag{"net"} | ngx::ToSink{&sink} |
                 ngx::ToSink{&legacy_sink} | Annotation{"COMPONENT", "db"} |
                 Annotation{"ATTEMPT", "2"},
             "retry {}", 2);
  });
  const auto after = std::chrono::system_clock::now();
  ASSERT_EQ(sink.received.size(), 1U);
  const RecordingStructuredSink::Received& record = sink.received.front();
  EXPECT_EQ(record.severity, NGLOG_WARNING);
  EXPECT_EQ(record.text, "retry 2");
  ASSERT_EQ(legacy_sink.lines.size(), 1U);
  EXPECT_EQ(record.line,
            static_cast<std::uint_least32_t>(legacy_sink.lines.front()));
  EXPECT_THAT(record.function, HasSubstr("StructuredSinkReceivesRecord"));
  EXPECT_EQ(record.tag, "net");
  EXPECT_LE(before, record.time);
  EXPECT_LE(record.time, after);
  EXPECT_THAT(record.fields, ElementsAre("COMPONENT=db", "ATTEMPT=2"));
  EXPECT_THAT(output.stderr_text, HasSubstr("retry 2"));
}

#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
// The zoned time of a record uses the time zone of the log files.
TEST_F(ExperimentalLogging, StructuredSinkRecordUsesTimeZoneOfLogFiles) {
  for (const bool utc : {false, true}) {
    const auto flags =
        nglog::MakeScopedFlags(nglog::MakeScopedFlag(FLAGS_log_utc_time, utc));
    ZoneSink sink;
    const Output output =
        Capture([&sink] { ngx::Log(ngx::Info | ngx::ToSink{&sink}, "zoned"); });
    const std::chrono::time_zone* const expected_zone =
        utc ? std::chrono::locate_zone("UTC") : std::chrono::current_zone();
    // Unlike the zone, which matches either way where the current one is UTC,
    // the setting itself must match.
    EXPECT_EQ(sink.utc, utc);
    EXPECT_EQ(sink.zone, expected_zone) << "utc: " << utc;
    EXPECT_EQ(sink.offset, sink.utc_offset) << "utc: " << utc;
    EXPECT_TRUE(sink.same_time) << "utc: " << utc;
  }
}
#endif

// Mixing structured and legacy sinks keeps the output of LOG_TO_SINK().
TEST_F(ExperimentalLogging, StructuredSinkLogsLikeMacro) {
  RecordingSink expected_sink;
  RecordingSink actual_sink;
  RecordingStructuredSink structured_sink;
  EXPECT_SAME_OUTPUT(LOG_TO_SINK(&expected_sink, WARNING) << "both",
                     ngx::Log(ngx::Warning | ngx::ToSink{&actual_sink} |
                                  ngx::ToSink{&structured_sink},
                              "both"));
  EXPECT_EQ(actual_sink.messages, expected_sink.messages);
  ASSERT_EQ(structured_sink.received.size(), 1U);
  EXPECT_EQ(structured_sink.received.front().text, "both");
}

TEST_F(ExperimentalLogging, StructuredSinkWithoutLogFile) {
  RecordingSink expected_sink;
  RecordingStructuredSink structured_sink;
  EXPECT_SAME_OUTPUT(
      LOG_TO_SINK_BUT_NOT_TO_LOGFILE(&expected_sink, WARNING) << "only",
      ngx::Log(ngx::Warning | ngx::ToSink{&structured_sink} | ngx::SkipLogFile,
               "only"));
  ASSERT_EQ(structured_sink.received.size(), 1U);
  EXPECT_EQ(structured_sink.received.front().text, "only");
}

// Fields are collected only for structured sinks.
TEST_F(ExperimentalLogging, FieldsCollectedOnlyForStructuredSinks) {
  int count = 0;
  RecordingStructuredSink sink;
  const Output output = Capture([&count, &sink] {
    ngx::Log(ngx::Info | CountedFields{&count}, "plain");
    ngx::Log(ngx::Info | CountedFields{&count} | ngx::ToSink{&sink},
             "structured");
  });
  EXPECT_EQ(count, 1);
  EXPECT_THAT(output.stderr_text, HasSubstr("structured"));
}

TEST_F(ExperimentalLogging, StringMatchesMacro) {
  std::string expected;
  std::string actual;
  EXPECT_SAME_OUTPUT(
      LOG_TO_STRING(ERROR, &expected) << "to string",
      ngx::Log(ngx::Error | ngx::ToString{&actual}, "to string"));
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(actual, "to string");
}

// Each of several strings receives the text as a single string does.
TEST_F(ExperimentalLogging, RepeatedStringsEachReceiveText) {
  std::string expected;
  std::string first;
  std::string second;
  EXPECT_SAME_OUTPUT(
      LOG_TO_STRING(ERROR, &expected) << "to strings",
      ngx::Log(ngx::Error | ngx::ToString{&first} | ngx::ToString{&second},
               "to strings"));
  EXPECT_EQ(first, expected);
  EXPECT_EQ(second, expected);
  EXPECT_EQ(first, "to strings");
}

TEST_F(ExperimentalLogging, VectorMatchesMacro) {
  std::vector<std::string> expected;
  std::vector<std::string> actual;
  EXPECT_SAME_OUTPUT(
      LOG_STRING(ERROR, &expected) << "to vector",
      ngx::Log(ngx::Error | ngx::ToVector{&actual}, "to vector"));
  EXPECT_EQ(actual, expected);
  EXPECT_THAT(actual, ElementsAre("to vector"));
}

// Each of several vectors receives the text as a single vector does.
TEST_F(ExperimentalLogging, RepeatedVectorsEachReceiveText) {
  std::vector<std::string> expected;
  std::vector<std::string> first;
  std::vector<std::string> second;
  EXPECT_SAME_OUTPUT(
      LOG_STRING(ERROR, &expected) << "to vectors",
      ngx::Log(ngx::Error | ngx::ToVector{&first} | ngx::ToVector{&second},
               "to vectors"));
  EXPECT_EQ(first, expected);
  EXPECT_EQ(second, expected);
  EXPECT_THAT(first, ElementsAre("to vectors"));
}

// Null vectors are ignored, so a message with a vector is not logged.
TEST_F(ExperimentalLogging, NullVectorAmongVectorsIsIgnored) {
  std::vector<std::string> expected;
  std::vector<std::string> actual;
  EXPECT_SAME_OUTPUT(
      LOG_STRING(ERROR, &expected) << "to vector",
      ngx::Log(ngx::Error | ngx::ToVector{&actual} | ngx::ToVector{nullptr},
               "to vector"));
  EXPECT_EQ(actual, expected);
}

// A message whose vectors are all null is logged like with LOG_STRING().
TEST_F(ExperimentalLogging, NullVectorsLogMessage) {
  EXPECT_SAME_OUTPUT(
      LOG_STRING(ERROR, nullptr) << "logged",
      ngx::Log(ngx::Error | ngx::ToVector{nullptr} | ngx::ToVector{nullptr},
               "logged"));
}

TEST_F(ExperimentalLogging, SyslogMatchesMacro) {
  EXPECT_SAME_OUTPUT(SYSLOG(INFO) << "to syslog",
                     ngx::Log(ngx::Info | ngx::Syslog, "to syslog"));
}

#if defined(__GLIBC__)
}  // namespace

// Records the messages sent to syslog instead of sending them. Definitions in
// the executable take precedence over those of the C library, also for calls
// from the library. Fortified builds call __syslog_chk() instead of syslog().
std::vector<std::string> g_syslog_messages;

namespace {

void RecordSyslog(const char* format, std::va_list args) {
  std::va_list copy;
  va_copy(copy, args);
  const int size = std::vsnprintf(nullptr, 0, format, copy);
  va_end(copy);
  std::string text(static_cast<std::size_t>(size), '\0');
  std::vsnprintf(text.data(), text.size() + 1, format, args);
  g_syslog_messages.push_back(std::move(text));
}

}  // namespace

extern "C" void syslog(int /*priority*/, const char* format, ...) {
  std::va_list args;
  va_start(args, format);
  RecordSyslog(format, args);
  va_end(args);
}

// NOLINTNEXTLINE(bugprone-reserved-identifier): the fortified syslog().
extern "C" void __syslog_chk(int /*priority*/, int /*flag*/, const char* format,
                             ...) {
  std::va_list args;
  va_start(args, format);
  RecordSyslog(format, args);
  va_end(args);
}

namespace {

TEST_F(ExperimentalLogging, SyslogSendsToSyslog) {
  g_syslog_messages.clear();
  Capture([] { ngx::Log(ngx::Info | ngx::Syslog, "to syslog"); });
  EXPECT_THAT(g_syslog_messages, ElementsAre("to syslog"));
}
#endif

// Prepends the name of a component to the message.
struct Component {
  std::string_view name;

  void Prefix(ngx::Message& message) const {
    message.Append("[");
    message.Append(name);
    message.Append("] ");
  }
};

// Appends a fixed marker to the message.
struct Marker {
  void Suffix(ngx::Message& message) const { message.Append(" <end>"); }
};

// Appends a label after the message. A message may carry several labels.
struct Label {
  using Multiplicity = ngx::Multiple;

  std::string_view text;

  void Suffix(ngx::Message& message) const {
    std::format_to(message.out(), " #{}", text);
  }
};

// Logs only while a switch is on.
struct Switch {
  const std::atomic<bool>* on;

  bool Admit(ngx::Context&) const {
    return on->load(std::memory_order_relaxed);
  }
};

// Logs a site only once using per-site state.
struct Once {
  struct State {
    std::atomic_flag logged;
  };

  bool Throttle(ngx::Context& context) const {
    return !context.site().state<State>().logged.test_and_set(
        std::memory_order_relaxed);
  }
};

// Raises the severity of a message to at least a warning.
struct Escalate {
  void Configure(ngx::Context& context) const {
    if (context.severity() < NGLOG_WARNING) {
      context.set_severity(NGLOG_WARNING);
    }
  }
};

// Logs while constructing its per-site state, such as a diagnostic.
struct LoggingState {
  struct State {
    State() { ngx::Log(ngx::First{1}, "constructing state"); }
  };

  bool Throttle(ngx::Context& context) const {
    context.site().state<State>();
    return true;
  }
};

// Constructing the state of a site may log at another site.
TEST_F(ExperimentalLogging, StateConstructorMayLog) {
  using namespace std::chrono_literals;
  constexpr auto kTimeout = 5s;
  std::promise<void> logged;
  std::future<void> done = logged.get_future();
  std::thread logger{[&logged] {
    ngx::Log(ngx::Info | LoggingState{}, "with state");
    logged.set_value();
  }};
  const std::future_status status = done.wait_for(kTimeout);
  if (status == std::future_status::ready) {
    logger.join();
  } else {
    // The logger deadlocked and can no longer be joined.
    logger.detach();
  }
  EXPECT_EQ(status, std::future_status::ready);
}

// Lets two threads construct the state of the same site concurrently.
std::latch g_both_constructing{2};

// Logs while destroying its per-site state, such as a summary of suppressed
// messages. The state that loses the race to be published is destroyed.
struct LoggingDestructor {
  struct State {
    State() { g_both_constructing.arrive_and_wait(); }
    State(const State& other) = delete;
    State& operator=(const State& other) = delete;
    ~State() { ngx::Log(ngx::First{1}, "destroying state"); }
  };

  bool Throttle(ngx::Context& context) const {
    context.site().state<State>();
    return true;
  }
};

// Destroying the state of a site that lost the race may log at another site.
TEST_F(ExperimentalLogging, StateDestructorMayLog) {
  using namespace std::chrono_literals;
  constexpr auto kTimeout = 5s;
  const auto log = [] { ngx::Log(ngx::Info | LoggingDestructor{}, "racing"); };
  std::promise<void> logged;
  std::future<void> done = logged.get_future();
  std::thread runner{[&log, &logged] {
    std::thread first{log};
    std::thread second{log};
    first.join();
    second.join();
    logged.set_value();
  }};
  const std::future_status status = done.wait_for(kTimeout);
  if (status == std::future_status::ready) {
    runner.join();
  } else {
    // The loggers deadlocked and can no longer be joined.
    runner.detach();
  }
  EXPECT_EQ(status, std::future_status::ready);
}

// Counts the messages it prefixes, which are those that are logged.
std::atomic<int> g_prefixed{0};

struct CountPrefixed {
  void Prefix(ngx::Message& /*message*/) const {
    g_prefixed.fetch_add(1, std::memory_order_relaxed);
  }
};

// Discards the messages it receives.
class DiscardingSink : public nglog::LogSink {
 public:
  void send(nglog::LogSeverity /*severity*/, const char* /*full_filename*/,
            const char* /*base_filename*/, int /*line*/,
            const nglog::LogMessageTime& /*time*/, const char* /*message*/,
            std::size_t /*message_len*/) override {}
};

// Threads arriving together log at most once per period, as documented.
TEST_F(ExperimentalLogging, EveryPeriodLogsOncePerPeriodAcrossThreads) {
  using namespace std::chrono_literals;
  constexpr int kThreads = 16;
  constexpr int kTrials = 15;
  constexpr auto kPeriod = 250ms;
  constexpr auto kPause = 300ms;
  DiscardingSink sink;
  std::vector<int> logged_per_trial;
  for (int trial = 0; trial != kTrials; ++trial) {
    // Lets the period elapse so that the next call logs again.
    std::this_thread::sleep_for(kPause);
    g_prefixed.store(0, std::memory_order_relaxed);
    std::latch start{kThreads};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i != kThreads; ++i) {
      threads.emplace_back([&start, &sink, kPeriod] {
        start.arrive_and_wait();
        ngx::Log(ngx::Every{kPeriod} | CountPrefixed{} | ngx::ToSink{&sink} |
                     ngx::SkipLogFile,
                 "tick");
      });
    }
    for (std::thread& thread : threads) {
      thread.join();
    }
    logged_per_trial.push_back(g_prefixed.load(std::memory_order_relaxed));
  }
  EXPECT_THAT(logged_per_trial, Each(1));
}

// An attribute composes with a predefined set and sets with each other.
TEST_F(ExperimentalLogging, ComposesSets) {
  constexpr auto kFirst = ngx::Attributes{ngx::First{1}};
  constexpr auto kWarning = ngx::Attributes{ngx::Warning};
  EXPECT_SAME_OUTPUT(LOG(WARNING) << "attribute and set",
                     ngx::Log(ngx::Warning | kFirst, "attribute and set"));
  EXPECT_SAME_OUTPUT(LOG(WARNING) << "two sets",
                     ngx::Log(kWarning | kFirst, "two sets"));
}

// The predefined attributes are single objects shared by all translation
// units, so that inline functions using them refer to the same entities.
TEST(ExperimentalAttributes, PredefinedAttributesHaveExternalLinkage) {
  EXPECT_THAT(PredefinedAttributesOfAnotherTranslationUnit(),
              ElementsAre(&ngx::Info, &ngx::Warning, &ngx::Error, &ngx::Fatal,
                          &ngx::FatalInDebug, &ngx::Errno, &ngx::Debug,
                          &ngx::Occurrence, &ngx::Syslog, &ngx::SkipLogFile,
                          &ngx::SignalSafe));
}

TEST_F(ExperimentalLogging, UserPrefixAndSuffix) {
  EXPECT_SAME_OUTPUT(
      LOG(INFO) << "[net] connected <end>",
      ngx::Log(ngx::Info | Component{"net"} | Marker{}, "connected"));
}

TEST_F(ExperimentalLogging, UserAdmission) {
  std::atomic<bool> on{false};
  EXPECT_THAT(Capture([&on] {
                ngx::Log(ngx::Info | Switch{&on}, "gated");
              }).stderr_text,
              IsEmpty());
  on.store(true, std::memory_order_relaxed);
  EXPECT_THAT(Capture([&on] {
                ngx::Log(ngx::Info | Switch{&on}, "gated");
              }).stderr_text,
              HasSubstr("gated"));
}

TEST_F(ExperimentalLogging, IsEnabledEvaluatesUserAttributes) {
  std::atomic<bool> on{false};
  EXPECT_FALSE(ngx::IsEnabled(ngx::Info | Switch{&on}));
  on.store(true, std::memory_order_relaxed);
  EXPECT_TRUE(ngx::IsEnabled(ngx::Info | Switch{&on}));
}

TEST_F(ExperimentalLogging, UserThrottleKeepsStatePerSite) {
  std::vector<std::string> messages;
  for (int i = 0; i != kRepetitions; ++i) {
    messages.push_back(Capture([i] {
                         ngx::Log(ngx::Info | Once{}, "once {}", i);
                       }).stderr_text);
  }
  EXPECT_THAT(messages[0], HasSubstr("once 0"));
  for (std::size_t i = 1; i != messages.size(); ++i) {
    EXPECT_THAT(messages[i], IsEmpty());
  }
  // Another call site has a state of its own.
  EXPECT_THAT(
      Capture([] { ngx::Log(ngx::Info | Once{}, "other site"); }).stderr_text,
      HasSubstr("other site"));
}

TEST_F(ExperimentalLogging, UserConfiguration) {
  EXPECT_SAME_OUTPUT(LOG(WARNING) << "escalated",
                     ngx::Log(ngx::Info | Escalate{}, "escalated"));
}

TEST_F(ExperimentalLogging, UserAttributeTag) {
  struct TagProbe {
    std::string* observed;

    bool Admit(ngx::Context& context) const {
      *observed = context.tag() == nullptr ? "" : context.tag();
      return false;
    }
  };

  std::string observed;
  ngx::Log(ngx::Info | ngx::Tag{"network"} | TagProbe{&observed}, "probe");
  EXPECT_EQ(observed, "network");
}

TEST_F(ExperimentalLogging, AttributesCompose) {
  EXPECT_SAME_OUTPUT(
      LOG(ERROR) << "[db] composed",
      ngx::Log(ngx::Attributes{ngx::Error, Component{"db"}}, "composed"));
}

template <class A>
concept OccurrenceLoggable = requires(const A& attributes) {
  ngx::Log(attributes, "{}", ngx::Occurrence);
};

// Only rate limits that count occurrences provide them.
static_assert(OccurrenceLoggable<ngx::Every<int>>,
              "Occurrence formats the occurrences that Every<int> counts");
static_assert(
    OccurrenceLoggable<ngx::First<std::size_t>>,
    "Occurrence formats the occurrences that First<std::size_t> counts");
static_assert(
    OccurrenceLoggable<decltype(ngx::Error | ngx::Every{2})>,
    "Occurrence formats the occurrences that Error | Every{2} counts");
static_assert(
    !OccurrenceLoggable<ngx::Severity>,
    "Occurrence is rejected with Severity, which counts no occurrences");
static_assert(!OccurrenceLoggable<ngx::Every<std::chrono::nanoseconds>>,
              "Occurrence is rejected with Every<nanoseconds>, which counts no "
              "occurrences");

template <class L, class R>
concept Composable = requires(const L& l, const R& r) {
  { l | r } -> ngx::AttributeSet;
};

static_assert(Composable<ngx::Severity, ngx::Tag>,
              "Severity and Tag compose into one set of attributes");
static_assert(Composable<ngx::Severity, Component>,
              "Severity and Component compose into one set of attributes");
static_assert(!Composable<ngx::Severity, ngx::Severity>,
              "Severity and Severity must not compose, since a message has "
              "only one of them");
static_assert(Composable<decltype(ngx::Error), decltype(ngx::FatalInDebug)>,
              "Error and FatalInDebug compose into one set of attributes");
static_assert(
    !Composable<decltype(ngx::FatalInDebug), decltype(ngx::FatalInDebug)>,
    "FatalInDebug and FatalInDebug must not compose, since a message has only "
    "one of them");
// A message has a single severity, also when it is added to a set.
static_assert(
    !Composable<decltype(ngx::Info | ngx::Tag{"a"}), decltype(ngx::Fatal)>,
    "Info | Tag{\"a\"} and Fatal must not compose, since a message has only "
    "one of them");
static_assert(!Composable<ngx::Severity, int>,
              "Severity and int must not compose, since int is no attribute");
static_assert(!Composable<ngx::ToSink, ngx::ToString>,
              "ToSink and ToString must not compose, since a message has only "
              "one of them");
static_assert(Composable<ngx::ToString, ngx::ToString>,
              "ToString and ToString compose into one set of attributes");
static_assert(Composable<ngx::ToVector, ngx::ToVector>,
              "ToVector and ToVector compose into one set of attributes");
static_assert(!Composable<ngx::ToString, ngx::ToVector>,
              "ToString and ToVector must not compose, since a message has "
              "only one of them");
static_assert(!Composable<ngx::Every<int>, ngx::First<>>,
              "Every<int> and First<> must not compose, since a message has "
              "only one of them");
static_assert(!Composable<ngx::Every<int>, ngx::Every<std::size_t>>,
              "Every<int> and Every<std::size_t> must not compose, since a "
              "message has only one of them");

// Attributes are single unless they declare multiple multiplicity, such as the
// sinks a message is sent to.
static_assert(Composable<ngx::ToSink, ngx::ToSink>,
              "ToSink and ToSink compose into one set of attributes");
static_assert(Composable<decltype(ngx::Info | Annotation{}), Annotation>,
              "Info | Annotation and Annotation compose, since Annotation may "
              "repeat");
static_assert(Composable<ngx::ToSink, ngx::LogFileExclusion>,
              "ToSink and LogFileExclusion compose into one set of attributes");
static_assert(Composable<ngx::LogFileExclusion, ngx::ToSink>,
              "LogFileExclusion and ToSink compose into one set of attributes");
static_assert(!Composable<ngx::LogFileExclusion, ngx::LogFileExclusion>,
              "LogFileExclusion and LogFileExclusion must not compose, since a "
              "message has only one of them");
static_assert(
    Composable<decltype(ngx::ToSink{} | ngx::ToSink{}), ngx::ToSink>,
    "ToSink{} | ToSink{} and ToSink compose into one set of attributes");
static_assert(
    !Composable<decltype(ngx::ToSink{} | ngx::ToSink{}), ngx::ToString>,
    "ToSink{} | ToSink{} and ToString must not compose, since a message has "
    "only one of them");
static_assert(
    !Composable<ngx::Tag, ngx::Tag>,
    "Tag and Tag must not compose, since a message has only one of them");
static_assert(!Composable<ngx::ErrnoDescription, ngx::ErrnoDescription>,
              "ErrnoDescription and ErrnoDescription must not compose, since a "
              "message has only one of them");
static_assert(!Composable<ngx::DebugOnly, ngx::DebugOnly>,
              "DebugOnly and DebugOnly must not compose, since a message has "
              "only one of them");
static_assert(!Composable<ngx::SyslogDestination, ngx::SyslogDestination>,
              "SyslogDestination and SyslogDestination must not compose, since "
              "a message has only one of them");
static_assert(!Composable<decltype(ngx::Info | Component{}), Component>,
              "Info | Component{} and Component must not compose, since a "
              "message has only one of them");
static_assert(Composable<decltype(ngx::Info | Label{}), Label>,
              "Info | Label{} and Label compose into one set of attributes");
static_assert(Composable<decltype(ngx::Info | Label{} | Label{}), Component>,
              "Info | Label{} | Label{} and Component compose into one set of "
              "attributes");

// Like DCHECK_IS_ON(), debug mode follows the calling translation unit.
static_assert(ngx::IsDebugEnabled() == static_cast<bool>(DCHECK_IS_ON()),
              "IsDebugEnabled() must agree with DCHECK_IS_ON() of the calling "
              "translation unit");

// Counters follow the type of the argument, and so do periods. First defaults
// to int like Verbose.
static_assert(std::is_same_v<decltype(ngx::Every{1}), ngx::Every<int>>,
              "Every{1} must deduce Every<int>");
static_assert(std::is_same_v<decltype(ngx::Every{std::size_t{1}}),
                             ngx::Every<std::size_t>>,
              "Every{std::size_t{1}} must deduce Every<std::size_t>");
static_assert(std::is_same_v<decltype(ngx::First{1L}), ngx::First<long>>,
              "First{1L} must deduce First<long>");
static_assert(std::is_same_v<ngx::First<>, ngx::First<int>>,
              "First<> must default to First<int>");

static_assert(std::is_same_v<decltype(ngx::Verbose{1}), ngx::Verbose<int>>,
              "Verbose{1} must deduce Verbose<int>");
static_assert(std::is_same_v<decltype(ngx::Verbose{std::size_t{1}}),
                             ngx::Verbose<std::size_t>>,
              "Verbose{std::size_t{1}} must deduce Verbose<std::size_t>");
static_assert(std::is_same_v<ngx::Verbose<>, ngx::Verbose<int>>,
              "Verbose<> must default to Verbose<int>");
namespace chrono_literals_examples {

using namespace std::chrono_literals;

static_assert(
    std::is_same_v<decltype(ngx::Every{1s}), ngx::Every<std::chrono::seconds>>,
    "Every{1s} must deduce Every<seconds>");

static_assert(std::is_same_v<decltype(ngx::Every{500ms}),
                             ngx::Every<std::chrono::milliseconds>>,
              "Every{500ms} must deduce Every<milliseconds>");
// The representation of floating-point literals is implementation-defined.
static_assert(
    std::is_same_v<decltype(ngx::Every{0.5s}), ngx::Every<decltype(0.5s)>>,
    "Every{0.5s} must deduce Every<decltype(0.5s)>");

// The argument selects whether Every counts occurrences like LOG_EVERY_N() or
// limits the rate to a period like LOG_EVERY_T().
static_assert(ngx::Every{2}.n == 2,
              "Every counts occurrences for an integer argument");
static_assert(ngx::Every{500ms}.period == 500ms,
              "Every limits by period for a duration argument");
// Both limit the rate of a message, which has a single rate limit.
static_assert(!Composable<ngx::Every<int>, ngx::Every<std::chrono::seconds>>,
              "Every<int> and Every<seconds> must not compose, since a message "
              "has only one of them");

}  // namespace chrono_literals_examples

// Other arguments are rejected with a message naming the expected ones, which
// the build tests verify.
static_assert(std::is_same_v<decltype(ngx::Every{std::uint64_t{1}}),
                             ngx::Every<std::uint64_t>>,
              "Every{std::uint64_t{1}} must deduce Every<std::uint64_t>");
static_assert(std::is_same_v<decltype(ngx::Every{std::int8_t{1}}),
                             ngx::Every<std::int8_t>>,
              "Every{std::int8_t{1}} must deduce Every<std::int8_t>");

template <class T>
concept VerbosityType = requires { typename ngx::Verbose<T>; };

static_assert(VerbosityType<short>, "Verbose accepts levels of type short");
static_assert(
    !VerbosityType<bool>,
    "Verbose rejects levels of type bool, which is no standard integer");
static_assert(
    !VerbosityType<double>,
    "Verbose rejects levels of type double, which is no standard integer");

static_assert(!Composable<decltype(ngx::Info | ngx::Tag{"a"}), ngx::Tag>,
              "Info | Tag{\"a\"} and Tag must not compose, since a message has "
              "only one of them");
static_assert(
    Composable<decltype(ngx::Info | ngx::Tag{"a"}), ngx::ErrnoDescription>,
    "Info | Tag{\"a\"} and ErrnoDescription compose into one set of "
    "attributes");

// The logger of a two-stage call is used once as a temporary, since a stored
// one would report state captured long before the call.
template <class L>
concept CallableAsLvalue = requires(L& logger) { logger("{}", 1); };

template <class L>
concept CallableAsRvalue =
    requires(L&& logger) { static_cast<L&&>(logger)("{}", 1); };

using ErrnoLogger = decltype(ngx::Log(ngx::Error | ngx::Errno));
static_assert(!CallableAsLvalue<ErrnoLogger>,
              "the two-stage logger ErrnoLogger is callable only as the "
              "temporary of its call");
static_assert(!CallableAsLvalue<const ErrnoLogger>,
              "the two-stage logger const ErrnoLogger is callable only as the "
              "temporary of its call");
static_assert(CallableAsRvalue<ErrnoLogger>,
              "the two-stage logger ErrnoLogger is callable as the temporary "
              "of its call");

}  // namespace
