// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Verifies that Log() and Ensure() with SignalSafe produce the output of
// RAW_LOG() without allocating memory, also for floating-point arguments.

#include "ng-log/experimental/signal_safe_log.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#if __has_include(<stdfloat>)
#  include <stdfloat>  // IWYU pragma: keep
#endif

#include "ng-log/experimental/ensure.h"
#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "ng-log/raw_logging.h"
#include "testing.h"
#include "testing_utilities.h"
#include "utilities.h"

#if __has_include(<fcntl.h>) && __has_include(<unistd.h>)
#  include <fcntl.h>
#  include <unistd.h>
#  define NGLOG_TEST_HAVE_POSIX_DESCRIPTORS
#endif

namespace {

using namespace ngx_testing;
using testing::ContainerEq;
using testing::ElementsAre;
using testing::HasSubstr;
using testing::IsEmpty;
using testing::Not;

#if defined(NGLOG_TEST_HAVE_POSIX_DESCRIPTORS)
//! Replaces the standard error descriptor with one that rejects writes and
//! restores it afterwards.
class ScopedUnwritableStderr {
 public:
  ScopedUnwritableStderr() : saved_{dup(STDERR_FILENO)} {
    const nglog::FileDescriptor read_only{open("/dev/null", O_RDONLY)};
    dup2(read_only.get(), STDERR_FILENO);
  }
  ~ScopedUnwritableStderr() { dup2(saved_.get(), STDERR_FILENO); }

  ScopedUnwritableStderr(const ScopedUnwritableStderr&) = delete;
  ScopedUnwritableStderr& operator=(const ScopedUnwritableStderr&) = delete;

 private:
  nglog::FileDescriptor saved_;
};

// A signal handler must not change the errno the interrupted code observes,
// also if writing the message fails.
TEST_F(ExperimentalLogging, SignalSafeLogKeepsErrno) {
  const ScopedUnwritableStderr unwritable;
  errno = EACCES;
  ngx::Log(ngx::SignalSafe | ngx::Error, "unwritable");
  EXPECT_EQ(errno, EACCES);
}
#endif

TEST_F(ExperimentalLogging, SignalSafeLogMatchesMacro) {
  EXPECT_SAME_OUTPUT(RAW_LOG(WARNING, "raw %d %s %c", 42, "text", 'x'),
                     ngx::Log(ngx::SignalSafe | ngx::Warning, "raw {} {} {}",
                              42, "text", 'x'));
}

TEST_F(ExperimentalLogging, SignalSafeLogDefaultsToInfo) {
  EXPECT_SAME_OUTPUT(RAW_LOG(INFO, "raw"), ngx::Log(ngx::SignalSafe, "raw"));
}

TEST_F(ExperimentalLogging, SignalSafeLogTruncatesLikeMacro) {
  constexpr std::size_t kLength = 5000;
  const std::string text(kLength, 'x');
  EXPECT_SAME_OUTPUT(RAW_LOG(INFO, "%s", text.c_str()),
                     ngx::Log(ngx::SignalSafe, "{}", text));
  EXPECT_THAT(
      Capture([&text] { ngx::Log(ngx::SignalSafe, "{}", text); }).stderr_text,
      HasSubstr("RAW_LOG ERROR: The Message was too long!"));
}

TEST_F(ExperimentalLogging, SignalSafeLogSuppressedLikeMacro) {
  nglog::SetFlag(FLAGS_logtostderr, false);
  nglog::SetFlag(
      FLAGS_stderrthreshold,
      static_cast<std::underlying_type_t<nglog::LogSeverity>>(NGLOG_ERROR));
  EXPECT_SAME_OUTPUT(RAW_LOG(WARNING, "quiet"),
                     ngx::Log(ngx::SignalSafe | ngx::Warning, "quiet"));
  EXPECT_THAT(Capture([] {
                ngx::Log(ngx::SignalSafe | ngx::Warning, "quiet");
              }).stderr_text,
              IsEmpty());
}

TEST_F(ExperimentalLogging, SignalSafeLogFatalMatchesMacro) {
  EXPECT_SAME_DEATH(RAW_LOG(FATAL, "raw %d", 1),
                    ngx::Log(ngx::SignalSafe | ngx::Fatal, "raw {}", 1));
}

#ifdef GTEST_HAS_DEATH_TEST
// A signal-safe check terminates like RAW_CHECK() through the signal-safe
// destination, after the prefix of a failed check.
TEST_F(ExperimentalLogging, SignalSafeEnsureFailsLikeRawCheck) {
  const bool ready = false;
  EXPECT_DEATH(ngx::Ensure(ready, ngx::SignalSafe, "not {}", "ready"),
               "RAW: Check failed: not ready");
}

#  if DCHECK_IS_ON()
TEST_F(ExperimentalLogging, SignalSafeDebugEnsureFailsLikeRawDcheck) {
  const bool ready = false;
  EXPECT_DEATH(ngx::Ensure(ready, ngx::SignalSafe | ngx::Debug, "not ready"),
               "RAW: Check failed: not ready");
}
#  endif
#endif

#if !DCHECK_IS_ON()
TEST_F(ExperimentalLogging, SignalSafeDebugEnsureIsDisabled) {
  EXPECT_THAT(Capture([] {
                ngx::Ensure(false, ngx::SignalSafe | ngx::Debug, "not ready");
              }).stderr_text,
              IsEmpty());
}
#endif

TEST_F(ExperimentalLogging, SignalSafeEnsurePassesSilently) {
  EXPECT_THAT(Capture([] {
                ngx::Ensure(true, ngx::SignalSafe, "not ready");
              }).stderr_text,
              IsEmpty());
}

TEST_F(ExperimentalLogging, SignalSafeVerboseEnabledMatchesMacro) {
  nglog::SetFlag(FLAGS_v, static_cast<std::int32_t>(2));
  EXPECT_SAME_OUTPUT(
      RAW_VLOG(2, "raw verbose %d", 1),
      ngx::Log(ngx::SignalSafe | ngx::Verbose{2}, "raw verbose {}", 1));
}

TEST_F(ExperimentalLogging, SignalSafeVerboseDisabledMatchesMacro) {
  nglog::SetFlag(FLAGS_v, static_cast<std::int32_t>(1));
  EXPECT_SAME_OUTPUT(
      RAW_VLOG(2, "raw verbose %d", 1),
      ngx::Log(ngx::SignalSafe | ngx::Verbose{2}, "raw verbose {}", 1));
}

TEST_F(ExperimentalLogging, SignalSafeLogDebugMatchesMacro) {
  EXPECT_SAME_OUTPUT(RAW_DLOG(INFO, "debug %d", 2),
                     ngx::Log(ngx::SignalSafe | ngx::Debug, "debug {}", 2));
}

std::atomic<int> g_allocations{0};

void CountAllocation() { ++g_allocations; }

// Counts the allocations of the global operator new while in scope.
class ScopedAllocationCounter final {
 public:
  ScopedAllocationCounter() {
    g_allocations = 0;
    nglog::g_new_hook = &CountAllocation;
  }
  ScopedAllocationCounter(const ScopedAllocationCounter&) = delete;
  ScopedAllocationCounter& operator=(const ScopedAllocationCounter&) = delete;
  ~ScopedAllocationCounter() { nglog::g_new_hook = nullptr; }

  int count() const noexcept { return g_allocations.load(); }
};

// Escapes a deliberate allocation so the compiler cannot elide it.
std::unique_ptr<int> g_escaped;

TEST_F(ExperimentalLogging, AllocationCounterObservesAllocations) {
  int allocations = 0;
  {
    const ScopedAllocationCounter counter;
    g_escaped = std::make_unique<int>(1);
    allocations = counter.count();
  }
  g_escaped.reset();
  EXPECT_GT(allocations, 0);
}

// Counts the allocations of a logging call after a first call, which may
// initialize function-local statics.
template <class F>
int CountAllocations(const F& log) {
  log();
  const ScopedAllocationCounter counter;
  log();
  return counter.count();
}

// The std::format of the Microsoft C++ standard library constructs a
// std::string for digit grouping whenever it formats an integer with a format
// specification. Iterator debugging allocates a proxy for every std::string,
// including an empty one.
#if defined(_MSVC_STL_VERSION) && _ITERATOR_DEBUG_LEVEL != 0
constexpr int kSpecifiedIntegerAllocations = 1;
#else
constexpr int kSpecifiedIntegerAllocations = 0;
#endif

// Each argument type is counted separately, so that a failure names the type
// whose formatting allocates.
TEST_F(ExperimentalLogging, SignalSafeLogDoesNotAllocate) {
  const std::string text = "string";
  const int value = 7;
  const void* const pointer = &value;
  constexpr unsigned kHex = 255;
  std::vector<std::pair<std::string, int>> allocations;
  std::vector<std::pair<std::string, int>> expected;
  const auto count = [&allocations, &expected](const char* type,
                                               const auto& log,
                                               int expected_count = 0) {
    const int allocated = CountAllocations(log);
    allocations.emplace_back(type, allocated);
    expected.emplace_back(type, expected_count);
  };
  const Output output = Capture([&count, &text, pointer, kHex] {
    count("int", [] { ngx::Log(ngx::SignalSafe | ngx::Warning, "{}", 1); });
    count("long", [] { ngx::Log(ngx::SignalSafe | ngx::Warning, "{}", -2L); });
    count("bool", [] { ngx::Log(ngx::SignalSafe | ngx::Warning, "{}", true); });
    count("char", [] { ngx::Log(ngx::SignalSafe | ngx::Warning, "{}", 'c'); });
    count("C string",
          [] { ngx::Log(ngx::SignalSafe | ngx::Warning, "{}", "literal"); });
    count("std::string",
          [&text] { ngx::Log(ngx::SignalSafe | ngx::Warning, "{}", text); });
    count("aligned std::string_view", [] {
      ngx::Log(ngx::SignalSafe | ngx::Warning, "{:>6}",
               std::string_view{"view"});
    });
    count(
        "alternate hexadecimal",
        [kHex] { ngx::Log(ngx::SignalSafe | ngx::Warning, "{:#x}", kHex); },
        kSpecifiedIntegerAllocations);
    count("pointer", [pointer] {
      ngx::Log(ngx::SignalSafe | ngx::Warning, "{}", pointer);
    });
    count("nullptr",
          [] { ngx::Log(ngx::SignalSafe | ngx::Warning, "{}", nullptr); });
    count("filled C string",
          [] { ngx::Log(ngx::SignalSafe | ngx::Warning, "{:L>5}", "x"); });
    count(
        "all of them",
        [&text, pointer, kHex] {
          ngx::Log(ngx::SignalSafe | ngx::Warning,
                   "{} {} {} {} {} {} {:>6} {:#x} {} {} {:L>5}", 1, -2L, true,
                   'c', "literal", text, std::string_view{"view"}, kHex,
                   pointer, nullptr, "x");
        },
        kSpecifiedIntegerAllocations);
  });
  EXPECT_THAT(allocations, ContainerEq(expected));
  EXPECT_THAT(output.stderr_text,
              HasSubstr("RAW: 1 -2 true c literal string   view 0xff 0x"));
  EXPECT_THAT(output.stderr_text, HasSubstr(" 0x0 LLLLx"));
}

// Messages longer than the internal buffers of std::format implementations,
// which libc++ grows on the heap for other output iterators, do not allocate.
TEST_F(ExperimentalLogging, LongSignalSafeLogDoesNotAllocate) {
  constexpr std::size_t kLength = 1000;
  const std::string text(kLength, 'x');
  int allocations = -1;
  const Output output = Capture([&text, &allocations] {
    ngx::Log(ngx::SignalSafe | ngx::Warning, "{}", 0);
    const ScopedAllocationCounter counter;
    ngx::Log(ngx::SignalSafe | ngx::Warning, "{} {}", text, 1);
    allocations = counter.count();
  });
  EXPECT_EQ(allocations, 0);
  EXPECT_THAT(output.stderr_text, HasSubstr(text + " 1"));
}

// Returns the message text of the first raw log line in the output.
std::string SignalSafeMessage(const std::string& output) {
  constexpr std::string_view kMarker = "RAW: ";
  const std::size_t start = output.find(kMarker);
  if (start == std::string::npos) {
    return {};
  }
  const std::size_t text = start + kMarker.size();
  return output.substr(text, output.find('\n', text) - text);
}

// Formats a floating-point value signal safely and using std::format() with
// the same format string, which must produce identical text.
#define EXPECT_RAW_FLOAT(format_string, value)                           \
  EXPECT_EQ(SignalSafeMessage(Capture([&] {                              \
                                ngx::Log(ngx::SignalSafe, format_string, \
                                         value);                         \
                              }).stderr_text),                           \
            std::format(format_string, value))                           \
      << "format: " << (format_string)

// Some standard libraries, such as libc++, describe NaN payloads in
// std::to_chars, as in -nan(ind), but not in std::format.
TEST_F(ExperimentalLogging, SignalSafeLogFormatsNaNLikeStdFormat) {
  const double negative_nan = -std::numeric_limits<double>::quiet_NaN();
  constexpr double kSignalingNan = std::numeric_limits<double>::signaling_NaN();
  EXPECT_RAW_FLOAT("{}", negative_nan);
  EXPECT_RAW_FLOAT("{}", kSignalingNan);
  EXPECT_RAW_FLOAT("{:*^12E}", negative_nan);
  EXPECT_RAW_FLOAT("{:+.3g}", kSignalingNan);
  EXPECT_RAW_FLOAT("{:010}", negative_nan);
}

TEST_F(ExperimentalLogging, SignalSafeLogFormatsFloatsLikeStdFormat) {
  constexpr double kInfinity = std::numeric_limits<double>::infinity();
  constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
  constexpr float kFloat = 1.5F;
  constexpr long double kLongDouble = 1.25L;

  EXPECT_RAW_FLOAT("{}", 3.14);
  EXPECT_RAW_FLOAT("{}", 1e300);
  EXPECT_RAW_FLOAT("{}", -0.0);
  EXPECT_RAW_FLOAT("{}", kInfinity);
  EXPECT_RAW_FLOAT("{}", -kInfinity);
  EXPECT_RAW_FLOAT("{}", kNan);
  EXPECT_RAW_FLOAT("{}", kFloat);
  EXPECT_RAW_FLOAT("{}", kLongDouble);
  EXPECT_RAW_FLOAT("{:.3}", 3.14159);
  EXPECT_RAW_FLOAT("{:e}", 3.14);
  EXPECT_RAW_FLOAT("{:.3E}", 1234.5);
  EXPECT_RAW_FLOAT("{:f}", 1e300);
  EXPECT_RAW_FLOAT("{:.64f}", 0.1);
  EXPECT_RAW_FLOAT("{:.0f}", 2.5);
  EXPECT_RAW_FLOAT("{:F}", kInfinity);
  EXPECT_RAW_FLOAT("{:g}", 1e-10);
  EXPECT_RAW_FLOAT("{:G}", 1e20);
  EXPECT_RAW_FLOAT("{:a}", 1.0);
  EXPECT_RAW_FLOAT("{:.3A}", 3.14);
  EXPECT_RAW_FLOAT("{:+}", 2.5);
  EXPECT_RAW_FLOAT("{: }", 2.5);
  EXPECT_RAW_FLOAT("{:+}", kNan);
  EXPECT_RAW_FLOAT("{:>12.3f}", 3.14159);
  EXPECT_RAW_FLOAT("{:*<10}", 1.5);
  EXPECT_RAW_FLOAT("{:^11.2e}", 1e5);
  EXPECT_RAW_FLOAT("{:010.3f}", -3.14159);
  EXPECT_RAW_FLOAT("{:+08.2f}", 1.5);
  EXPECT_RAW_FLOAT("{:<08}", 1.5);
  EXPECT_RAW_FLOAT("{:010}", kInfinity);
  EXPECT_RAW_FLOAT("{:.64e}", 1e-300);
  EXPECT_RAW_FLOAT("{:e}", kLongDouble);
  EXPECT_RAW_FLOAT("{:.20g}", kLongDouble);
}

TEST_F(ExperimentalLogging, SignalSafeFloatsDoNotAllocate) {
  // Exceeds the range of double where long double is wider.
  constexpr long double kLongDouble = std::numeric_limits<long double>::max();
  int allocations = -1;
  const Output output = Capture([&allocations, kLongDouble] {
    // Warm up function-local statics, which may allocate once.
    ngx::Log(ngx::SignalSafe | ngx::Warning, "{}", 0.0);
    const ScopedAllocationCounter counter;
    ngx::Log(ngx::SignalSafe | ngx::Warning,
             "{} {:f} {:.64e} {:+012.3f} {:a} {:.64g}", 3.14, 1e300, 1e-300,
             -2.5F, 1.0, kLongDouble);
    allocations = counter.count();
  });
  EXPECT_EQ(allocations, 0);
  EXPECT_THAT(output.stderr_text, HasSubstr("RAW: 3.14 1000000000000000052"));
}

template <class... Args>
concept SignalSafeLoggable =
    requires(const Args&... args) { ngx::Log(ngx::SignalSafe, "", args...); };

template <class A>
concept SignalSafeLoggableWith = requires(const A& attributes) {
  ngx::Log(ngx::SignalSafe | attributes, "");
};

// Only types whose formatting never allocates are accepted.
static_assert(SignalSafeLoggable<int, unsigned long long, bool, char>,
              "SignalSafe formats int, unsigned long long, bool, char without "
              "allocating");
static_assert(SignalSafeLoggable<const char*, std::string, std::string_view>,
              "SignalSafe formats const char*, std::string, std::string_view "
              "without allocating");
static_assert(
    SignalSafeLoggable<const void*, std::nullptr_t>,
    "SignalSafe formats const void*, std::nullptr_t without allocating");
static_assert(
    SignalSafeLoggable<float, double, long double>,
    "SignalSafe formats float, double, long double without allocating");
static_assert(!SignalSafeLoggable<Formattable>,
              "SignalSafe rejects Formattable, which cannot be formatted "
              "without allocating");
// Extended floating-point types fail the constraints instead of the formatting
// inside the header, since FormatSignalSafeFloat() does not support them.
#ifdef __STDCPP_FLOAT16_T__
static_assert(!SignalSafeLoggable<std::float16_t>,
              "SignalSafe rejects std::float16_t, which cannot be formatted "
              "without allocating");
#endif
#ifdef __STDCPP_BFLOAT16_T__
static_assert(!SignalSafeLoggable<std::bfloat16_t>,
              "SignalSafe rejects std::bfloat16_t, which cannot be formatted "
              "without allocating");
#endif
#ifdef __STDCPP_FLOAT128_T__
static_assert(!SignalSafeLoggable<std::float128_t>,
              "SignalSafe rejects std::float128_t, which cannot be formatted "
              "without allocating");
#endif
// Neither may allocate or acquire locks.
static_assert(!SignalSafeLoggable<decltype(ngx::Lazy([] { return 1; }))>,
              "SignalSafe rejects Lazy([] { return 1; }), which cannot be "
              "formatted without allocating");
// Only attributes that neither allocate nor lock are accepted.
static_assert(SignalSafeLoggableWith<ngx::Severity>,
              "SignalSafe combines with Severity");
static_assert(SignalSafeLoggableWith<ngx::DebugOnly>,
              "SignalSafe combines with DebugOnly");
static_assert(SignalSafeLoggableWith<decltype(ngx::Warning | ngx::Debug)>,
              "SignalSafe combines with Warning | Debug");
static_assert(SignalSafeLoggableWith<ngx::Verbose<>>,
              "SignalSafe combines with Verbose<>");
static_assert(SignalSafeLoggableWith<decltype(ngx::Debug | ngx::Verbose{2})>,
              "SignalSafe combines with Debug | Verbose{2}");
static_assert(!SignalSafeLoggableWith<ngx::Every<int>>,
              "SignalSafe rejects Every<int>, which may allocate, lock or "
              "select another destination");
static_assert(!SignalSafeLoggableWith<ngx::Tag>,
              "SignalSafe rejects Tag, which may allocate, lock or select "
              "another destination");
static_assert(!SignalSafeLoggableWith<decltype(ngx::Warning | ngx::Errno)>,
              "SignalSafe rejects Warning | Errno, which may allocate, lock or "
              "select another destination");
// SignalSafe is a destination, which excludes the others regardless of
// whether they are signal safe.
template <class A>
concept ComposableWithSignalSafe =
    requires(const A& attributes) { ngx::SignalSafe | attributes; };

static_assert(ComposableWithSignalSafe<ngx::Severity>,
              "SignalSafe composes with a severity");
static_assert(!ComposableWithSignalSafe<ngx::SyslogDestination>,
              "SignalSafe must not compose with Syslog, since both are "
              "destinations");

template <class Condition>
concept SignalSafeCheckable = requires(Condition&& condition) {
  ngx::Ensure(std::forward<Condition>(condition), ngx::SignalSafe, "");
};

// Describing comparisons and null checks allocates, so only conditions can be
// checked signal safely.
static_assert(SignalSafeCheckable<bool>, "a signal-safe Ensure accepts a bool");
static_assert(
    !SignalSafeCheckable<ngx::Comparison>,
    "a signal-safe Ensure rejects a Comparison, whose description allocates");
static_assert(!SignalSafeCheckable<ngx::NotNullCheck<const int*>>,
              "a signal-safe Ensure rejects a NotNullCheck<const int*>, whose "
              "description allocates");
static_assert(!SignalSafeCheckable<ngx::OperandComparison<const int&>>,
              "a signal-safe Ensure rejects an OperandComparison<const int&>, "
              "whose description allocates");

// Logging has no effect during constant evaluation.
constexpr int SignalSafeLogged(int value) {
  ngx::Log(ngx::SignalSafe | ngx::Warning, "value {}", value);
  return value;
}

static_assert(SignalSafeLogged(2) == 2,
              "a signal-safe Log() has no effect during constant evaluation");

}  // namespace
