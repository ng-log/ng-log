// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Helpers shared by the tests of the experimental logging functions.

#ifndef NGLOG_SRC_EXPERIMENTAL_TESTING_H
#define NGLOG_SRC_EXPERIMENTAL_TESTING_H

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <regex>
#include <source_location>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "ng-log/experimental/attributes.h"
#include "ng-log/experimental/ensure.h"
#include "ng-log/experimental/sink.h"
#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "testing_utilities.h"

namespace nglog::base::internal {

bool GetExitOnDFatal();
void SetExitOnDFatal(bool value);

}  // namespace nglog::base::internal

namespace nglog::experimental {

// Prints the failure description of a comparison or the expression text of
// one that holds. Also prints a comparison created by That(), which the generic
// printer of GoogleTest would match better than a function taking its base.
template <std::derived_from<Comparison> C>
void PrintTo(const C& comparison, std::ostream* stream) {
  if (comparison.holds()) {
    *stream << internal::ComparisonFactory::Text(comparison);
  } else {
    *stream << internal::ComparisonFactory::Description(comparison);
  }
}

}  // namespace nglog::experimental

namespace ngx_testing {

namespace ngx = nglog::experimental;

#ifdef NGLOG_NO_ABBREVIATED_SEVERITIES
// Once windows.h defines ERROR, PLOG(ERROR) refers to this severity without
// qualification.
using nglog::NGLOG_0;
#endif

using testing::HasSubstr;

// Number of the attributes and arguments the headers predefine as variables.
inline constexpr std::size_t kPredefinedAttributes = 12;

// Addresses of the predefined attributes as another translation unit sees
// them: Info, Warning, Error, Fatal, FatalInDebug, Errno, Debug, Occurrence,
// Syslog, SkipLogFile, SignalSafe and Async.
std::array<const void*, kPredefinedAttributes>
PredefinedAttributesOfAnotherTranslationUnit();

// Matches a comparison that holds. Failures print the comparison, which
// describes it like the failure message of Ensure().
MATCHER(Holds, negation ? "does not hold" : "holds") { return arg.holds(); }

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

// Adds a field to the message. A message may carry several of them.
struct Annotation {
  using Multiplicity = ngx::Multiple;

  std::string_view name;
  std::string_view value;

  void Fields(ngx::FieldWriter& fields) const { fields.Add(name, value); }
};

// Output of a logging statement written to stderr and to a sink.
struct Output {
  std::string stderr_text;
  std::vector<std::string> sink_messages;
};

// Records every message a sink receives.
class RecordingSink final : public nglog::LogSink {
 public:
  void send(nglog::LogSeverity severity, const char* full_filename,
            const char* base_filename, int line, const nglog::LogMessageTime&,
            const char* message, std::size_t message_len) override {
    // Lines are recorded separately since they depend on how the call sites
    // are wrapped.
    messages.push_back(std::format(
        "{}|{}|{}|{}",
        static_cast<std::underlying_type_t<nglog::LogSeverity>>(severity),
        full_filename, base_filename, std::string_view{message, message_len}));
    lines.push_back(line);
  }

  std::vector<std::string> messages;
  std::vector<int> lines;
};

// Replaces the timestamps and line numbers of every default prefix by fixed
// strings. A macro nested in the argument of another macro reports the line
// of the argument while std::source_location reports the line of the outer
// macro, so lines are compared by a separate test.
inline std::string MaskTimestamps(const std::string& text) {
  static const std::regex prefix{
      R"(([IWEF])\d{8} \d{2}:\d{2}:\d{2}\.\d{6}( +[0-9A-Fa-fx]+ [^:]+):\d+\])"};
  return std::regex_replace(text, prefix, "$1<timestamp>$2:<line>]");
}

// Makes fatal log messages throw instead of terminating while in scope.
class ScopedThrowOnFatal final {
 public:
  ScopedThrowOnFatal()
      : previous_{nglog::InstallFailureFunction(&ThrowFatalLogFailure)} {}
  ScopedThrowOnFatal(const ScopedThrowOnFatal&) = delete;
  ScopedThrowOnFatal& operator=(const ScopedThrowOnFatal&) = delete;
  ~ScopedThrowOnFatal() { nglog::InstallFailureFunction(previous_); }

 private:
  [[noreturn]] static void ThrowFatalLogFailure() {
    throw std::logic_error{"fatal log"};
  }

  nglog::logging_fail_func_t previous_;
};

// Overrides whether DFATAL messages terminate while in scope.
class ScopedExitOnDFatal final {
 public:
  explicit ScopedExitOnDFatal(bool value)
      : previous_{nglog::base::internal::GetExitOnDFatal()} {
    nglog::base::internal::SetExitOnDFatal(value);
  }
  ScopedExitOnDFatal(const ScopedExitOnDFatal&) = delete;
  ScopedExitOnDFatal& operator=(const ScopedExitOnDFatal&) = delete;
  ~ScopedExitOnDFatal() { nglog::base::internal::SetExitOnDFatal(previous_); }

 private:
  bool previous_;
};

// Runs a statement while capturing stderr and returns what it wrote together
// with the messages a registered sink received.
template <class Statement>
Output Capture(Statement statement) {
  RecordingSink sink;
  nglog::AddLogSink(&sink);

  Output output;
  {
    nglog::ScopedStreamCapture capture{nglog::StandardStream::kStderr};
    try {
      statement();
    } catch (const std::logic_error&) {
      // A fatal message ends the statement early. Its output is compared
      // regardless.
    }
    output.stderr_text = MaskTimestamps(capture.Stop());
  }

  nglog::RemoveLogSink(&sink);
  output.sink_messages = std::move(sink.messages);
  // Messages the library reports to sinks, such as the lack of syslog support
  // on Windows, may embed a log line with its own timestamp.
  for (std::string& message : output.sink_messages) {
    message = MaskTimestamps(message);
  }
  return output;
}

// Expands both statements on the same line so that the macro and the function
// report an identical source location.
#define EXPECT_SAME_OUTPUT(macro_statement, function_statement)           \
  do {                                                                    \
    const Output macro_output = Capture([&] { macro_statement; });        \
    const Output function_output = Capture([&] { function_statement; });  \
    EXPECT_EQ(function_output.stderr_text, macro_output.stderr_text);     \
    EXPECT_EQ(function_output.sink_messages, macro_output.sink_messages); \
  } while (false)

// Masks the thread ID of a line whose timestamp is masked. Death tests run each
// statement in its own process, which has a distinct thread ID on Windows.
inline std::string MaskThreadId(const std::string& line) {
  static const std::regex thread_id{R"(^([IWEF]<timestamp>) +[0-9A-Fa-fx]+ )"};
  return std::regex_replace(line, thread_id, "$1 <thread> ");
}

// Line that precedes the stack trace of a fatal message.
inline constexpr std::string_view kStackTraceMarker =
    "*** Check failure stack trace: ***";

// Beginning of a stack frame line, which a fatal raw message writes without
// the marker.
inline constexpr std::string_view kStackFramePrefix = "    @ ";

// Matches any text and stores its lines up to the stack trace, whose frames
// differ between call sites, with masked timestamps and thread IDs.
inline testing::Matcher<const std::string&> RecordMessage(std::string* lines) {
  return testing::Truly([lines](const std::string& text) {
    lines->clear();
    std::istringstream stream{text};
    std::string line;
    while (std::getline(stream, line) && line != kStackTraceMarker &&
           !line.starts_with(kStackFramePrefix)) {
      lines->append(MaskThreadId(MaskTimestamps(line)));
      lines->push_back('\n');
    }
    return true;
  });
}

// Terminates using both statements on the same line and compares what each one
// writes before the stack trace. Fatal messages are compared in separate
// processes because the library reuses the buffer of fatal messages. Separate
// functions keep the labels of both death tests apart.
#ifdef GTEST_HAS_DEATH_TEST
#  define EXPECT_SAME_DEATH(macro_statement, function_statement) \
    do {                                                         \
      std::string macro_message;                                 \
      std::string function_message;                              \
      [&] {                                                      \
        EXPECT_DEATH(([&] { macro_statement; }()),               \
                     RecordMessage(&macro_message));             \
      }();                                                       \
      [&] {                                                      \
        EXPECT_DEATH(([&] { function_statement; }()),            \
                     RecordMessage(&function_message));          \
      }();                                                       \
      EXPECT_EQ(function_message, macro_message);                \
      EXPECT_THAT(macro_message, HasSubstr("] "));               \
    } while (false)
#else
// Without death tests, both statements are compiled but never run.
#  define EXPECT_SAME_DEATH(macro_statement, function_statement) \
    do {                                                         \
      if (false) {                                               \
        macro_statement;                                         \
        function_statement;                                      \
      }                                                          \
      GTEST_SKIP() << "Death tests are not supported";           \
    } while (false)
#endif

class ExperimentalLogging : public testing::Test {
 protected:
  void SetUp() override {
    nglog::SetFlag(FLAGS_logtostderr, true);
    nglog::SetFlag(FLAGS_colorlogtostderr, false);
    nglog::SetFlag(FLAGS_minloglevel, static_cast<std::int32_t>(0));
    nglog::SetFlag(FLAGS_v, static_cast<std::int32_t>(0));
  }

  nglog::ScopedFlags<> flags_;
};

// Whether the macros honor --vmodule, which requires GNU extensions. The
// experimental functions honor it with every compiler.
#if defined(__GNUC__)
inline constexpr bool kMacrosHonorModuleLevels = true;
#else
inline constexpr bool kMacrosHonorModuleLevels = false;
#endif

// Whether braced initializer lists evaluate the arguments of constructors from
// left to right as the standard requires. MSVC 19.51 does not.
#if defined(_MSC_VER) && !defined(__clang__)
inline constexpr bool kOrderedBracedArguments = false;
#else
inline constexpr bool kOrderedBracedArguments = true;
#endif

// Number of times a rate-limited statement runs.
constexpr int kRepetitions = 7;
// Rate of rate-limited statements.
constexpr int kEvery = 3;

// Supports std::format but not stream insertion.
struct Formattable {
  int value;

  friend bool operator==(const Formattable&, const Formattable&) = default;
};

// Supports neither std::format nor stream insertion.
struct Unformattable {};

}  // namespace ngx_testing

template <>
struct std::formatter<ngx_testing::Formattable> : std::formatter<int> {
  auto format(const ngx_testing::Formattable& value,
              std::format_context& context) const {
    return std::formatter<int>::format(value.value, context);
  }
};

#endif  // NGLOG_SRC_EXPERIMENTAL_TESTING_H
