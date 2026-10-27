// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Verifies that Async passes messages to the logging thread, which writes them
// like the calling thread would.

#include "async.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <future>
#include <iterator>
#include <mutex>
#include <ostream>
#include <regex>
#include <semaphore>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "config.h"
#include "ng-log/experimental/attributes.h"
#include "ng-log/experimental/ensure.h"
#include "ng-log/experimental/log.h"
#include "ng-log/flags.h"
#include "ng-log/log_setup.h"
#include "ng-log/log_severity.h"
#include "ng-log/log_sink.h"
#include "ng-log/logging.h"
#include "ng-log/platform.h"
#include "testing.h"
#include "testing_utilities.h"

namespace {

using namespace ngx_testing;
using testing::AllOf;
using testing::Each;
using testing::ElementsAre;
using testing::ElementsAreArray;
using testing::Le;
using testing::Not;
using namespace std::chrono_literals;

// Bounds every wait of a test, which a correct implementation never reaches,
// so that a missing hand-off fails instead of hanging.
constexpr auto kSafetyTimeout = 30s;

// Records the threads that deliver the messages.
class ThreadRecordingSink final : public nglog::LogSink {
 public:
  void send(nglog::LogSeverity /*severity*/, const char* /*full_filename*/,
            const char* /*base_filename*/, int /*line*/,
            const nglog::LogMessageTime& /*time*/, const char* /*message*/,
            std::size_t /*message_len*/) override {
    const std::scoped_lock lock{mutex_};
    threads_.push_back(std::this_thread::get_id());
  }

  std::vector<std::thread::id> threads() const {
    const std::scoped_lock lock{mutex_};
    return threads_;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<std::thread::id> threads_;
};

// Delivers a message only once the test opens the gate.
class GatedSink final : public nglog::LogSink {
 public:
  void send(nglog::LogSeverity /*severity*/, const char* /*full_filename*/,
            const char* /*base_filename*/, int /*line*/,
            const nglog::LogMessageTime& /*time*/, const char* /*message*/,
            std::size_t /*message_len*/) override {
    entered_.release();
    opened_in_time_ = gate_.try_acquire_for(kSafetyTimeout);
  }

  // Waits until the logging thread waits for the gate, after which it holds
  // no lock while the test forks. Returns whether it did in time.
  bool WaitUntilEntered() { return entered_.try_acquire_for(kSafetyTimeout); }

  void Open() { gate_.release(); }

  // Whether the gate was opened while the message was being delivered.
  bool opened_in_time() const noexcept { return opened_in_time_; }

 private:
  std::binary_semaphore entered_{0};
  std::binary_semaphore gate_{0};
  bool opened_in_time_ = false;
};

// Records the times of the messages.
class TimeRecordingSink final : public nglog::LogSink {
 public:
  void send(nglog::LogSeverity /*severity*/, const char* /*full_filename*/,
            const char* /*base_filename*/, int /*line*/,
            const nglog::LogMessageTime& time, const char* /*message*/,
            std::size_t /*message_len*/) override {
    const std::scoped_lock lock{mutex_};
    times_.push_back(time.when());
  }

  std::vector<std::chrono::system_clock::time_point> times() const {
    const std::scoped_lock lock{mutex_};
    return times_;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<std::chrono::system_clock::time_point> times_;
};

// Counts the messages it receives.
class CountingSink final : public nglog::LogSink {
 public:
  void send(nglog::LogSeverity /*severity*/, const char* /*full_filename*/,
            const char* /*base_filename*/, int /*line*/,
            const nglog::LogMessageTime& /*time*/, const char* /*message*/,
            std::size_t /*message_len*/) override {
    count_.fetch_add(1, std::memory_order_relaxed);
  }

  std::size_t count() const noexcept {
    return count_.load(std::memory_order_relaxed);
  }

 private:
  std::atomic<std::size_t> count_{0};
};

// Records the messages as LogSink::ToString() formats them, optionally after
// logging a message of its own to another sink.
class FormattingSink final : public nglog::LogSink {
 public:
  FormattingSink() = default;
  explicit FormattingSink(CountingSink& nested) noexcept : nested_{&nested} {}

  void send(nglog::LogSeverity severity, const char* /*full_filename*/,
            const char* base_filename, int line,
            const nglog::LogMessageTime& time, const char* message,
            std::size_t message_len) override {
    if (nested_ != nullptr) {
      ngx::Log(ngx::Async | ngx::ToSink{nested_} | ngx::SkipLogFile, "nested");
    }
    const std::scoped_lock lock{mutex_};
    texts_.push_back(MaskTimestamps(
        ToString(severity, base_filename, line, time, message, message_len)));
  }

  std::vector<std::string> texts() const {
    const std::scoped_lock lock{mutex_};
    return texts_;
  }

 private:
  CountingSink* nested_ = nullptr;
  mutable std::mutex mutex_;
  std::vector<std::string> texts_;
};

// Flushes the logs while receiving a message.
class FlushingSink final : public nglog::LogSink {
 public:
  void send(nglog::LogSeverity /*severity*/, const char* /*full_filename*/,
            const char* /*base_filename*/, int /*line*/,
            const nglog::LogMessageTime& /*time*/, const char* /*message*/,
            std::size_t /*message_len*/) override {
    nglog::FlushLogFiles(NGLOG_INFO);
    flushed_.set_value();
  }

  std::future<void> flushed() { return flushed_.get_future(); }

 private:
  std::promise<void> flushed_;
};

// Logs a message to another sink while receiving one and records how many
// messages that sink received once the call returned.
class RelayingSink final : public nglog::LogSink {
 public:
  explicit RelayingSink(CountingSink& target) noexcept : target_{&target} {}

  void send(nglog::LogSeverity /*severity*/, const char* /*full_filename*/,
            const char* /*base_filename*/, int /*line*/,
            const nglog::LogMessageTime& /*time*/, const char* /*message*/,
            std::size_t /*message_len*/) override {
    ngx::Log(ngx::Async | ngx::ToSink{target_} | ngx::SkipLogFile, "relayed");
    received_on_return_ = target_->count();
  }

  std::size_t received_on_return() const noexcept {
    return received_on_return_;
  }

 private:
  CountingSink* target_;
  std::size_t received_on_return_ = 0;
};

// Keeps the logging thread busy for a while with every message it receives,
// so that later messages are still queued once the caller goes on.
class SlowSink final : public nglog::LogSink {
 public:
  void send(nglog::LogSeverity /*severity*/, const char* /*full_filename*/,
            const char* /*base_filename*/, int /*line*/,
            const nglog::LogMessageTime& /*time*/, const char* /*message*/,
            std::size_t /*message_len*/) override {
    std::this_thread::sleep_for(100ms);
  }
};

// Size of messages that fill the queue quickly and still fit a log message.
constexpr std::size_t kLargeTextSize = 16 * 1024;
// Twice the messages that fit the queue.
constexpr std::size_t kFillingMessages =
    2 * ngx::internal::kMaxPendingBytes / kLargeTextSize;
// Long enough for a producer that does not wait to log all messages.
constexpr auto kObservation = 1s;

// Holds the logging thread and logs large messages on a thread of its own
// until twice the capacity of the queue is logged, so that the queue fills up.
class QueueFiller {
 public:
  QueueFiller() {
    ngx::Log(ngx::Async | ngx::ToSink{&held_} | ngx::SkipLogFile, "held");
    holds_logging_thread_ = held_.WaitUntilEntered();
    producer_ = std::thread{[this] {
      for (std::size_t index = 0; index < kFillingMessages; ++index) {
        ngx::Log(ngx::Async | ngx::ToSink{&sink_} | ngx::SkipLogFile, "{}",
                 text_);
        logged_.fetch_add(1, std::memory_order_relaxed);
      }
      finished_.set_value();
    }};
  }

  QueueFiller(const QueueFiller& other) = delete;
  QueueFiller& operator=(const QueueFiller& other) = delete;

  ~QueueFiller() { Release(); }

  // Waits for the producer as long as one that does not wait for space would
  // take to log all messages.
  std::future_status WaitForProducer() {
    return producer_finished_.wait_for(kObservation);
  }

  std::size_t logged() const noexcept {
    return logged_.load(std::memory_order_relaxed);
  }

  // Lets the logging thread send the messages and waits until it did.
  void Release() {
    if (producer_.joinable()) {
      held_.Open();
      producer_.join();
      nglog::FlushLogFiles(NGLOG_INFO);
    }
  }

  std::size_t received() const noexcept { return sink_.count(); }

  // Whether the logging thread waits for the held message.
  bool holds_logging_thread() const noexcept { return holds_logging_thread_; }

 private:
  GatedSink held_;
  bool holds_logging_thread_ = false;
  CountingSink sink_;
  const std::string text_ = std::string(kLargeTextSize, 'x');
  std::atomic<std::size_t> logged_{0};
  std::promise<void> finished_;
  std::future<void> producer_finished_ = finished_.get_future();
  std::thread producer_;
};

// Records the threads that perform a step of composing a message.
class ThreadLog {
 public:
  void Record() {
    const std::scoped_lock lock{mutex_};
    threads_.push_back(std::this_thread::get_id());
  }

  void Record(std::thread::id thread) {
    const std::scoped_lock lock{mutex_};
    threads_.push_back(thread);
  }

  std::vector<std::thread::id> threads() const {
    const std::scoped_lock lock{mutex_};
    return threads_;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<std::thread::id> threads_;
};

// Formats as a fixed text and records the thread that formats it.
struct FormattingThread {
  ThreadLog* log;
};

// Records the threads that run its hooks that compose the message.
struct ComposingThread {
  ThreadLog* log;

  void Fields(ngx::FieldWriter& /*fields*/) const { log->Record(); }
  void Prefix(ngx::Message& /*message*/) const { log->Record(); }
  void Suffix(ngx::Message& /*message*/) const { log->Record(); }
};

// Threads that format a prefix and the threads that the formatted messages
// report.
struct PrefixThreads {
  ThreadLog formatting;
  ThreadLog reported;
};

// Installs a prefix formatter that records its threads while in scope.
class ScopedPrefixFormatter {
 public:
  explicit ScopedPrefixFormatter(PrefixThreads& threads) {
    nglog::InstallPrefixFormatter(&Format, &threads);
  }
  ScopedPrefixFormatter(const ScopedPrefixFormatter& other) = delete;
  ScopedPrefixFormatter& operator=(const ScopedPrefixFormatter& other) = delete;
  ~ScopedPrefixFormatter() { nglog::InstallPrefixFormatter(nullptr, nullptr); }

 private:
  static void Format(std::ostream& stream, const nglog::LogMessage& message,
                     void* data) {
    auto* const threads = static_cast<PrefixThreads*>(data);
    threads->formatting.Record();
    threads->reported.Record(message.thread_id());
    stream << "prefix";
  }
};

// Formats as the text composed so far and then throws.
struct ThrowingFormat {};

}  // namespace

template <>
struct std::formatter<FormattingThread> {
  constexpr std::format_parse_context::iterator parse(
      std::format_parse_context& context) {
    return context.begin();
  }

  std::format_context::iterator format(const FormattingThread& value,
                                       std::format_context& context) const {
    value.log->Record();
    return std::format_to(context.out(), "formatted");
  }
};

template <>
struct std::formatter<ThrowingFormat> {
  constexpr std::format_parse_context::iterator parse(
      std::format_parse_context& context) {
    return context.begin();
  }

  [[noreturn]] std::format_context::iterator format(
      const ThrowingFormat& /*value*/, std::format_context& /*context*/) const {
    throw std::runtime_error{"formatting"};
  }
};

namespace {

// Matches text that contains first and, after it, second.
MATCHER_P2(ContainsInOrder, first, second, "") {
  const std::string_view text{arg};
  const std::size_t position = text.find(first);
  return position != std::string_view::npos &&
         text.find(second, position + std::string_view{first}.size()) !=
             std::string_view::npos;
}

using ExperimentalAsync = ExperimentalLogging;

TEST_F(ExperimentalAsync, DeliversOnAnotherThread) {
  ThreadRecordingSink sink;
  ngx::Log(ngx::Async | ngx::ToSink{&sink} | ngx::SkipLogFile, "message");
  nglog::FlushLogFiles(NGLOG_INFO);
  EXPECT_THAT(sink.threads(), ElementsAre(Not(std::this_thread::get_id())));
}

// The caller returns while a destination is still busy with the message.
TEST_F(ExperimentalAsync, DoesNotWaitForDestinations) {
  GatedSink sink;
  ngx::Log(ngx::Async | ngx::ToSink{&sink} | ngx::SkipLogFile, "message");
  sink.Open();
  nglog::FlushLogFiles(NGLOG_INFO);
  EXPECT_TRUE(sink.opened_in_time());
}

// Sinks receive the time of the call instead of the time the logging thread
// sent the message, which a held logging thread delays.
TEST_F(ExperimentalAsync, KeepsTimeOfCall) {
  GatedSink held;
  TimeRecordingSink sink;
  ngx::Log(ngx::Async | ngx::ToSink{&held} | ngx::SkipLogFile, "held");
  ngx::Log(ngx::Async | ngx::ToSink{&sink} | ngx::SkipLogFile, "timed");
  const auto returned = std::chrono::system_clock::now();
  held.Open();
  nglog::FlushLogFiles(NGLOG_INFO);
  EXPECT_THAT(sink.times(), ElementsAre(Le(returned)));
}

// Sinks that format a message name the thread of the call like for a message
// the calling thread sends.
TEST_F(ExperimentalAsync, SinksFormatThreadOfCall) {
  FormattingSink sync_sink;
  FormattingSink async_sink;
  ngx::Log(ngx::ToSink{&sync_sink} | ngx::SkipLogFile, "message");
  ngx::Log(ngx::Async | ngx::ToSink{&async_sink} | ngx::SkipLogFile, "message");
  nglog::FlushLogFiles(NGLOG_INFO);
  EXPECT_THAT(async_sink.texts(), ElementsAreArray(sync_sink.texts()));
}

// Sinks that log a message of their own still format the thread of the call
// of the message they receive.
TEST_F(ExperimentalAsync, SinksFormatThreadOfCallAfterLogging) {
  CountingSink nested;
  FormattingSink sync_sink{nested};
  FormattingSink async_sink{nested};
  ngx::Log(ngx::ToSink{&sync_sink} | ngx::SkipLogFile, "message");
  ngx::Log(ngx::Async | ngx::ToSink{&async_sink} | ngx::SkipLogFile, "message");
  nglog::FlushLogFiles(NGLOG_INFO);
  EXPECT_THAT(async_sink.texts(), ElementsAreArray(sync_sink.texts()));
}

// Sinks receive the line of the call.
TEST_F(ExperimentalAsync, SinksReceiveLineOfCall) {
  RecordingSink sink;
  const int line = __LINE__ + 1;
  ngx::Log(ngx::Async | ngx::ToSink{&sink} | ngx::SkipLogFile, "located");
  nglog::FlushLogFiles(NGLOG_INFO);
  EXPECT_THAT(sink.lines, ElementsAre(line));
}

// Arguments are formatted on the logging thread.
TEST_F(ExperimentalAsync, FormatsArgumentsOnLoggingThread) {
  ThreadLog formatting;
  ngx::Log(ngx::Async, "{}", FormattingThread{&formatting});
  nglog::FlushLogFiles(NGLOG_INFO);
  EXPECT_THAT(formatting.threads(),
              ElementsAre(Not(std::this_thread::get_id())));
}

// The hooks that compose the message run on the logging thread.
TEST_F(ExperimentalAsync, ComposesOnLoggingThread) {
  ThreadLog composing;
  RecordingStructuredSink sink;
  ngx::Log(ngx::Async | ngx::ToSink{&sink} | ComposingThread{&composing},
           "composed");
  nglog::FlushLogFiles(NGLOG_INFO);
  // Fields, prefix and suffix.
  EXPECT_THAT(composing.threads(), Each(Not(std::this_thread::get_id())));
  EXPECT_THAT(composing.threads(), testing::SizeIs(3));
}

// An installed prefix formatter runs on the logging thread and reports the
// thread of the call.
TEST_F(ExperimentalAsync, FormatsPrefixOnLoggingThread) {
  PrefixThreads prefix;
  {
    const ScopedPrefixFormatter formatter{prefix};
    ngx::Log(ngx::Async, "prefixed");
    nglog::FlushLogFiles(NGLOG_INFO);
  }
  EXPECT_THAT(prefix.formatting.threads(),
              ElementsAre(Not(std::this_thread::get_id())));
  EXPECT_THAT(prefix.reported.threads(),
              ElementsAre(std::this_thread::get_id()));
}

// Arguments are copied, so that changing them after the call does not change
// the message, including C strings and string views, which are copied as
// strings.
TEST_F(ExperimentalAsync, CopiesArguments) {
  GatedSink held;
  RecordingSink sink;
  ngx::Log(ngx::Async | ngx::ToSink{&held} | ngx::SkipLogFile, "held");
  char buffer[] = "original";
  std::string viewed = "original";
  std::string text = "original";
  ngx::Log(ngx::Async | ngx::ToSink{&sink} | ngx::SkipLogFile, "{} {} {}",
           buffer, std::string_view{viewed}, text);
  std::ranges::fill(std::span{buffer}.first(std::size(buffer) - 1), 'x');
  std::ranges::fill(viewed, 'x');
  text = "changed";
  held.Open();
  nglog::FlushLogFiles(NGLOG_INFO);
  EXPECT_THAT(sink.messages,
              ElementsAre(testing::EndsWith("|original original original")));
}

// The tag reaches structured sinks although the caller changes it before the
// held logging thread sends the message.
TEST_F(ExperimentalAsync, KeepsSettingsOfCall) {
  GatedSink held;
  RecordingStructuredSink sink;
  ngx::Log(ngx::Async | ngx::ToSink{&held} | ngx::SkipLogFile, "held");
  std::string tag = "net";
  ngx::Log(ngx::Async | ngx::Tag{tag.c_str()} | ngx::ToSink{&sink} |
               ngx::SkipLogFile | Annotation{"COMPONENT", "db"},
           "configured");
  tag.replace(0, tag.size(), tag.size(), 'x');
  held.Open();
  nglog::FlushLogFiles(NGLOG_INFO);
  ASSERT_THAT(sink.received, testing::SizeIs(1));
  const RecordingStructuredSink::Received& received = sink.received.front();
  EXPECT_EQ(received.tag, "net");
  EXPECT_THAT(received.fields, ElementsAre("COMPONENT=db"));
  EXPECT_EQ(received.function, std::source_location::current().function_name());
  EXPECT_EQ(received.text, "configured");
}

#if defined(HAVE_STACKTRACE) && defined(HAVE_SYMBOLIZE)
// Logs the message whose call site --log_backtrace_at names.
NGLOG_ATTRIBUTE_NOINLINE void LogWithBacktrace(RecordingSink& sink, int& line) {
  line = __LINE__ + 1;
  ngx::Log(ngx::Async | ngx::ToSink{&sink} | ngx::SkipLogFile, "traced");
}

// The stack trace that --log_backtrace_at requests is the one of the call.
TEST_F(ExperimentalAsync, KeepsBacktraceOfCall) {
  RecordingSink sink;
  int line = 0;
  LogWithBacktrace(sink, line);
  nglog::FlushLogFiles(NGLOG_INFO);
  nglog::SetFlag(FLAGS_log_backtrace_at,
                 std::format("async_unittest.cc:{}", line));
  sink.messages.clear();
  LogWithBacktrace(sink, line);
  nglog::FlushLogFiles(NGLOG_INFO);
  ASSERT_THAT(sink.messages, testing::SizeIs(1));
#  if defined(HAVE_ADDR2LINE)
  if (sink.messages.front().find("LogWithBacktrace") == std::string::npos) {
    GTEST_SKIP() << "addr2line could not resolve LogWithBacktrace";
  }
#  endif
  EXPECT_THAT(sink.messages.front(),
              AllOf(HasSubstr("stacktrace:"), HasSubstr("LogWithBacktrace")));
}
#endif

#ifdef GTEST_HAS_DEATH_TEST
// An argument whose formatting throws on the logging thread terminates the
// program once the message is written, since the caller cannot receive the
// exception. std::format may discard the text it formatted before the
// argument, so only the prefix of the message is certain.
TEST_F(ExperimentalAsync, ThrowingArgumentTerminates) {
  EXPECT_DEATH(([] {
                 ngx::Log(ngx::Async, "before {}", ThrowingFormat{});
                 nglog::FlushLogFiles(NGLOG_INFO);
               }()),
               HasSubstr("async_unittest.cc:"));
}

// A fatal message terminates the caller once the messages passed to the
// logging thread before it are sent.
TEST_F(ExperimentalAsync, FatalTerminatesCallerAfterPendingMessages) {
  EXPECT_DEATH(([] {
                 ngx::Log(ngx::Async, "pending");
                 ngx::Log(ngx::Async | ngx::Fatal, "fatal");
                 std::fputs("continued\n", stderr);
                 std::fflush(stderr);
               }()),
               AllOf(ContainsInOrder("] pending", "] fatal"),
                     Not(HasSubstr("continued"))));
}

// A failed check terminates the caller like a fatal message.
TEST_F(ExperimentalAsync, FailedCheckTerminatesCallerAfterPendingMessages) {
  EXPECT_DEATH(([] {
                 ngx::Log(ngx::Async, "pending");
                 ngx::Ensure(false, ngx::Async, "failed");
               }()),
               ContainsInOrder("] pending", "Check failed: failed"));
}
#endif

#if defined(GTEST_HAS_DEATH_TEST) && !defined(NGLOG_OS_WINDOWS)
// Death tests run their statement in a fork of the test process, except on
// Windows, which starts a new process that runs the test from its beginning.

// A forked child, which lacks the logging thread of its parent, passes its
// messages to a logging thread of its own.
TEST_F(ExperimentalAsync, ForkedChildLogs) {
  ThreadRecordingSink sink;
  ngx::Log(ngx::Async | ngx::ToSink{&sink} | ngx::SkipLogFile, "parent");
  nglog::FlushLogFiles(NGLOG_INFO);
  EXPECT_EXIT(([] {
                ngx::Log(ngx::Async, "child");
                nglog::FlushLogFiles(NGLOG_INFO);
                std::exit(EXIT_SUCCESS);
              }()),
              testing::ExitedWithCode(EXIT_SUCCESS), HasSubstr("] child"));
}

// Messages pending in the parent while it forks are sent by the parent only.
TEST_F(ExperimentalAsync, ForkedChildDoesNotSendMessagesOfParent) {
  GatedSink held;
  ngx::Log(ngx::Async | ngx::ToSink{&held} | ngx::SkipLogFile, "held");
  ASSERT_TRUE(held.WaitUntilEntered()) << "the logging thread is not held";
  ngx::Log(ngx::Async, "inherited");
  EXPECT_EXIT(([] {
                // Does not wait for the message the parent sent while forking.
                nglog::FlushLogFiles(NGLOG_INFO);
                ngx::Log(ngx::Async, "child");
                nglog::FlushLogFiles(NGLOG_INFO);
                std::exit(EXIT_SUCCESS);
              }()),
              testing::ExitedWithCode(EXIT_SUCCESS),
              AllOf(HasSubstr("] child"), Not(HasSubstr("inherited"))));
  held.Open();
  nglog::FlushLogFiles(NGLOG_INFO);
}

// A child forked while the queue of its parent is full logs without waiting
// for the space the messages of its parent take.
TEST_F(ExperimentalAsync, ForkedChildStartsWithEmptyQueue) {
  QueueFiller filler;
  ASSERT_TRUE(filler.holds_logging_thread())
      << "the logging thread is not held";
  ASSERT_EQ(filler.WaitForProducer(), std::future_status::timeout);
  EXPECT_EXIT(([] {
                // Takes more than the space the full queue of the parent
                // leaves.
                ngx::Log(ngx::Async, "child {}",
                         std::string(kLargeTextSize, 'x'));
                nglog::FlushLogFiles(NGLOG_INFO);
                std::exit(EXIT_SUCCESS);
              }()),
              testing::ExitedWithCode(EXIT_SUCCESS), HasSubstr("] child"));
}

// Exit handlers that run once the logging thread stopped send their messages
// themselves, which a slow destination would otherwise keep from being sent
// before the program ends. Windows runs the exit handlers of a program before
// those of the library.
TEST_F(ExperimentalAsync, LateExitHandlersSendTheirMessages) {
  const std::string style = GTEST_FLAG_GET(death_test_style);
  // Runs the statement in a new process, so that the logging thread starts
  // after the exit handler is registered and stops before it runs.
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  EXPECT_EXIT(([] {
                std::atexit([] {
                  // Never destroyed, since the program exits.
                  auto* const slow = new SlowSink;
                  ngx::Log(ngx::Async | ngx::ToSink{slow}, "late");
                });
                ngx::Log(ngx::Async, "early");
                std::exit(EXIT_SUCCESS);
              }()),
              testing::ExitedWithCode(EXIT_SUCCESS),
              ContainsInOrder("] early", "] late"));
  GTEST_FLAG_SET(death_test_style, style);
}
#endif

// Flushing on the logging thread, such as in a sink, does not wait for the
// message being sent.
TEST_F(ExperimentalAsync, FlushingOnLoggingThreadReturns) {
  FlushingSink sink;
  std::future<void> flushed = sink.flushed();
  ngx::Log(ngx::Async | ngx::ToSink{&sink} | ngx::SkipLogFile, "message");
  EXPECT_EQ(flushed.wait_for(kSafetyTimeout), std::future_status::ready);
  // The sink is in use until the logging thread finished the message.
  nglog::FlushLogFiles(NGLOG_INFO);
}

// Callers wait while the messages pending for a held logging thread exceed the
// capacity of the queue, and no message is dropped.
TEST_F(ExperimentalAsync, CallersWaitWhileQueueIsFull) {
  QueueFiller filler;
  EXPECT_EQ(filler.WaitForProducer(), std::future_status::timeout);
  // The queue holds about the capacity before callers wait.
  EXPECT_GE(filler.logged() * kLargeTextSize,
            ngx::internal::kMaxPendingBytes / 2);
  filler.Release();
  EXPECT_EQ(filler.received(), kFillingMessages);
}

// The logging thread sends its own messages, such as those of a sink, before
// returning instead of waiting for space in the queue it drains.
TEST_F(ExperimentalAsync, LoggingThreadSendsItsMessagesItself) {
  CountingSink target;
  RelayingSink relay{target};
  ngx::Log(ngx::Async | ngx::ToSink{&relay} | ngx::SkipLogFile, "message");
  nglog::FlushLogFiles(NGLOG_INFO);
  EXPECT_EQ(relay.received_on_return(), 1);
}

#ifdef GTEST_HAS_DEATH_TEST
// Shutting down sends the pending messages first.
TEST_F(ExperimentalAsync, ShutdownSendsPendingMessages) {
  EXPECT_EXIT(([] {
                SlowSink slow;
                ngx::Log(ngx::Async | ngx::ToSink{&slow} | ngx::SkipLogFile,
                         "slow");
                ngx::Log(ngx::Async, "pending");
                nglog::ShutdownLogging();
                std::_Exit(EXIT_SUCCESS);
              }()),
              testing::ExitedWithCode(EXIT_SUCCESS), HasSubstr("] pending"));
}

// Exiting sends the pending messages first. On Windows, which terminates the
// logging thread before, the message it sends while exiting is lost.
TEST_F(ExperimentalAsync, ExitSendsPendingMessages) {
#  if defined(NGLOG_OS_WINDOWS)
  const auto expected = HasSubstr("] pending");
#  else
  const auto expected = ContainsInOrder("] slow", "] pending");
#  endif
  EXPECT_EXIT(([] {
                // Never destroyed, since exiting destroys static objects
                // constructed after the logging thread before sending the
                // pending messages.
                auto* const slow = new SlowSink;
                ngx::Log(ngx::Async | ngx::ToSink{slow}, "slow");
                ngx::Log(ngx::Async, "pending");
                std::exit(EXIT_SUCCESS);
              }()),
              testing::ExitedWithCode(EXIT_SUCCESS), expected);
}
#endif

#if !defined(NGLOG_OS_WINDOWS)
// Forces colored output to stderr while in scope.
class ExperimentalAsyncColors : public ExperimentalAsync {
 protected:
  void SetUp() override {
    ExperimentalAsync::SetUp();
    nglog::SetFlag(FLAGS_colorlogtostderr, true);
  }

 private:
  nglog::ScopedEnvironmentVariable clicolor_force_{"CLICOLOR_FORCE", "1"};
  nglog::ScopedEnvironmentVariable term_{"TERM", "xterm"};
  nglog::ScopedEnvironmentVariable no_color_{"NO_COLOR", nullptr};
};

// Replaces the numbers of colored output, such as of timestamps and lines,
// which the escape sequences keep the timestamp masks from matching.
std::string MaskNumbers(const std::string& text) {
  static const std::regex number{R"(\d+)"};
  return std::regex_replace(text, number, "<number>");
}

// The prefix fields and the errno description keep their colors.
TEST_F(ExperimentalAsyncColors, KeepsStyles) {
  std::string sync_text;
  std::string async_text;
  {
    nglog::ScopedStreamCapture capture{nglog::StandardStream::kStderr};
    errno = ENOENT;
    ngx::Log(ngx::Error | ngx::Errno, "colored");
    sync_text = MaskNumbers(capture.Stop());
  }
  {
    nglog::ScopedStreamCapture capture{nglog::StandardStream::kStderr};
    errno = ENOENT;
    ngx::Log(ngx::Async | ngx::Error | ngx::Errno, "colored");
    nglog::FlushLogFiles(NGLOG_INFO);
    async_text = MaskNumbers(capture.Stop());
  }
  ASSERT_THAT(sync_text, HasSubstr("\033["));
  EXPECT_EQ(async_text, sync_text);
}
#endif

}  // namespace
