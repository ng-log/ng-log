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

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace {

using nglog::nglog_testing::ScopedMockLog;
using std::string;
using testing::_;
using testing::EndsWith;
using testing::InSequence;
using testing::StrEq;

constexpr std::chrono::milliseconds kSinkCallbackTimeout{100};

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
  ~ReentrantLogSink() override { JoinThreads(); }

  void send(nglog::LogSeverity /* severity */, const char* /* full_filename */,
            const char* /* base_filename */, int /* line */,
            const nglog::LogMessageTime& /* time */, const char* /* message */,
            std::size_t /* message_len */) override {}

  void WaitTillSent() override {
    {
      std::lock_guard<std::mutex> lock{mutex_};
      if (started_) {
        return;
      }
      started_ = true;
    }

    add_thread_ = std::thread([this] {
      {
        std::lock_guard<std::mutex> lock{mutex_};
        add_started_ = true;
      }
      condition_.notify_all();
      nglog::AddLogSink(this);
      {
        std::lock_guard<std::mutex> lock{mutex_};
        add_finished_ = true;
      }
      condition_.notify_all();
    });

    {
      std::unique_lock<std::mutex> lock{mutex_};
      condition_.wait(lock, [this] { return add_started_; });
    }

    log_thread_ = std::thread([this] {
      LOG(INFO) << "Logging from a sink callback.";
      {
        std::lock_guard<std::mutex> lock{mutex_};
        log_finished_ = true;
      }
      condition_.notify_all();
    });

    std::unique_lock<std::mutex> lock{mutex_};
    callback_completed_ =
        condition_.wait_for(lock, kSinkCallbackTimeout,
                            [this] { return add_finished_ && log_finished_; });
  }

  bool CallbackCompleted() const {
    std::lock_guard<std::mutex> lock{mutex_};
    return callback_completed_;
  }

 private:
  void JoinThreads() {
    if (add_thread_.joinable()) {
      add_thread_.join();
    }
    if (log_thread_.joinable()) {
      log_thread_.join();
    }
  }

  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::thread add_thread_;
  std::thread log_thread_;
  bool started_{false};
  bool add_started_{false};
  bool add_finished_{false};
  bool log_finished_{false};
  bool callback_completed_{false};
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
  testing::InitGoogleTest(&argc, argv);
  testing::InitGoogleMock(&argc, argv);

  return RUN_ALL_TESTS();
}
