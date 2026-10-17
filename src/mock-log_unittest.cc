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
#include <thread>

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

}  // namespace

int main(int argc, char** argv) {
  nglog::InitializeLogging(argv[0]);
  // A shared GoogleMock library carries its own copy of GoogleTest, which
  // InitGoogleMock initializes instead of the one the tests run in.
  testing::InitGoogleTest(&argc, argv);
  testing::InitGoogleMock(&argc, argv);

  return RUN_ALL_TESTS();
}
