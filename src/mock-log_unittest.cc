// SPDX-FileCopyrightText: 2009-2023 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Zhanyong Wan

// Tests the ScopedMockLog class.

#include "mock-log.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include <vector>

#include "testing_utilities.h"

namespace {

using nglog::nglog_testing::ScopedMockLog;
using testing::_;
using testing::EndsWith;
using testing::InSequence;
using testing::StrEq;

// Tests that ScopedMockLog intercepts LOG()s when it's alive.
TEST(ScopedMockLogTest, InterceptsLog) {
  ScopedMockLog log;

  InSequence s;
  EXPECT_CALL(log, Log(NGLOG_WARNING, EndsWith("mock-log_unittest.cc"),
                       StrEq("Fishy.")));
  EXPECT_CALL(log, Log(NGLOG_INFO, _, StrEq("Working..."))).Times(2);
  EXPECT_CALL(log, Log(NGLOG_ERROR, _, StrEq("Bad!!")));

  LOG(WARNING) << "Fishy.";
  LOG(INFO) << "Working...";
  LOG(INFO) << "Working...";
  LOG(ERROR) << "Bad!!";
}

void LogBranch() { LOG(INFO) << "Logging a branch..."; }

void LogTree() { LOG(INFO) << "Logging the whole tree..."; }

void LogForest() {
  LOG(INFO) << "Logging the entire forest.";
  LOG(INFO) << "Logging the entire forest..";
  LOG(INFO) << "Logging the entire forest...";
}

// The purpose of the following test is to verify that intercepting logging
// continues to work properly if a LOG statement is executed within the scope
// of a mocked call.
TEST(ScopedMockLogTest, LogDuringIntercept) {
  ScopedMockLog log;
  InSequence s;
  EXPECT_CALL(log,
              Log(NGLOG_INFO, StrEq(__FILE__), StrEq("Logging a branch...")))
      .WillOnce(LogTree);
  EXPECT_CALL(
      log, Log(NGLOG_INFO, StrEq(__FILE__), StrEq("Logging the whole tree...")))
      .WillOnce(LogForest);
  EXPECT_CALL(log, Log(NGLOG_INFO, StrEq(__FILE__),
                       StrEq("Logging the entire forest.")));
  EXPECT_CALL(log, Log(NGLOG_INFO, StrEq(__FILE__),
                       StrEq("Logging the entire forest..")));
  EXPECT_CALL(log, Log(NGLOG_INFO, StrEq(__FILE__),
                       StrEq("Logging the entire forest...")));
  LogBranch();
}

TEST(ScopedMockLogTest, SendsToSinksInReverseRegistrationOrder) {
  ScopedMockLog first;
  ScopedMockLog second;
  InSequence s;
  EXPECT_CALL(second, Log(NGLOG_INFO, StrEq(__FILE__),
                          StrEq("Logging in registration order.")));
  EXPECT_CALL(first, Log(NGLOG_INFO, StrEq(__FILE__),
                         StrEq("Logging in registration order.")));

  LOG(INFO) << "Logging in registration order.";
}

class ReentrantLogSink : public nglog::LogSink {
 public:
  void send(nglog::LogSeverity /* severity */, const char* /* full_filename */,
            const char* /* base_filename */, int /* line */,
            const nglog::LogMessageTime& /* time */, const char* /* message */,
            std::size_t /* message_len */) override {}

  // Joining the threads deadlocks if a sink registry lock is held during the
  // callback. CTest reports such a hang by timing out the test.
  void WaitTillSent() override {
    if (started_.exchange(true)) {
      return;
    }

    bool add_started = false;
    std::thread add_thread([this, &add_started] {
      {
        std::lock_guard<std::mutex> lock{mutex_};
        add_started = true;
      }
      condition_.notify_all();
      nglog::AddLogSink(this);
    });

    {
      std::unique_lock<std::mutex> lock{mutex_};
      condition_.wait(lock, [&add_started] { return add_started; });
    }

    std::thread log_thread(
        [] { LOG(INFO) << "Logging from a sink callback."; });

    add_thread.join();
    log_thread.join();
    callback_completed_ = true;
  }

  bool CallbackCompleted() const { return callback_completed_; }

 private:
  std::mutex mutex_;
  std::condition_variable condition_;
  std::atomic<bool> started_{false};
  std::atomic<bool> callback_completed_{false};
};

TEST(ScopedMockLogTest, SinkCallbackCanLogWhileSinksChange) {
  ReentrantLogSink sink;
  nglog::AddLogSink(&sink);

  LOG(INFO) << "Start sink callback.";

  EXPECT_TRUE(sink.CallbackCompleted());
  nglog::RemoveLogSink(&sink);
}

// Discards every message.
class DiscardingLogSink : public nglog::LogSink {
 public:
  void send(nglog::LogSeverity /* severity */, const char* /* full_filename */,
            const char* /* base_filename */, int /* line */,
            const nglog::LogMessageTime& /* time */, const char* /* message */,
            size_t /* message_len */) override {}
};

// Logging to another sink also calls WaitTillSent() of every registered sink,
// which must not report a message that the mock did not receive.
TEST(ScopedMockLogTest, IgnoresMessagesOfOtherSinks) {
  ScopedMockLog log;
  EXPECT_CALL(log, Log(_, _, _)).Times(0);
  DiscardingLogSink other;
  LOG_TO_SINK_BUT_NOT_TO_LOGFILE(&other, INFO) << "logged elsewhere";
}

// A message logged to the mock as a sink of its own while the mock is
// registered reaches it twice, and each delivery is reported.
TEST(ScopedMockLogTest, ReportsEachDeliveryOfAMessage) {
  ScopedMockLog log;
  EXPECT_CALL(log, Log(_, _, StrEq("twice"))).Times(2);

  LOG_TO_SINK(&log, INFO) << "twice";
}

// Each thread must see the message that it logged itself. The threads start
// together to overlap as much as possible, since plain builds detect shared
// state only when the threads interleave, unlike ThreadSanitizer.
TEST(ScopedMockLogTest, InterceptsConcurrentLogs) {
  constexpr int kThreads = 4;
  constexpr int kMessagesPerThread = 2000;
  testing::NiceMock<ScopedMockLog> log;
  std::atomic<int> mismatches{0};
  ON_CALL(log, Log(_, _, _))
      .WillByDefault([&mismatches](nglog::LogSeverity, const std::string&,
                                   const std::string& message) {
        const std::string expected = std::to_string(
            std::hash<std::thread::id>{}(std::this_thread::get_id()));
        if (message != expected) {
          mismatches.fetch_add(1, std::memory_order_relaxed);
        }
      });

  std::atomic<int> waiting{kThreads};
  const auto log_own_id = [&waiting] {
    const std::string id = std::to_string(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
    waiting.fetch_sub(1, std::memory_order_acq_rel);
    while (waiting.load(std::memory_order_acquire) > 0) {
      std::this_thread::yield();
    }
    for (int i = 0; i < kMessagesPerThread; ++i) {
      LOG(INFO) << id;
    }
  };
  std::vector<std::thread> threads;
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back(log_own_id);
  }
  for (std::thread& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(mismatches.load(), 0);
}

}  // namespace

int main(int argc, char** argv) {
  nglog::InitializeTestLogging(argv[0]);
  // A shared GoogleMock library carries its own copy of GoogleTest, which
  // InitGoogleMock initializes instead of the one the tests run in.
  testing::InitGoogleTest(&argc, argv);
  testing::InitGoogleMock(&argc, argv);

  return RUN_ALL_TESTS();
}
