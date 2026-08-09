// Copyright (c) 2022, Google Inc.
// Copyright (c) 2026, The ng-log contributors
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
using testing::InvokeWithoutArgs;
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
      .WillOnce(InvokeWithoutArgs(LogTree));
  EXPECT_CALL(
      log, Log(NGLOG_INFO, StrEq(__FILE__), StrEq("Logging the whole tree...")))
      .WillOnce(InvokeWithoutArgs(LogForest));
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
