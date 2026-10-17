// SPDX-FileCopyrightText: 2007-2023 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Zhanyong Wan
//
// Defines the ScopedMockLog class (using Google C++ Mocking
// Framework), which is convenient for testing code that uses LOG().

#ifndef NGLOG_SRC_MOCK_LOG_H_
#define NGLOG_SRC_MOCK_LOG_H_

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <deque>
#include <string>
#include <unordered_map>
#include <utility>

#include "ng-log/logging.h"

namespace nglog {
namespace nglog_testing {

// A ScopedMockLog object intercepts LOG() messages issued during its
// lifespan.  Using this together with Google C++ Mocking Framework,
// it's very easy to test how a piece of code calls LOG().  The
// typical usage:
//
//   TEST(FooTest, LogsCorrectly) {
//     ScopedMockLog log;
//
//     // We expect the WARNING "Something bad!" exactly twice.
//     EXPECT_CALL(log, Log(WARNING, _, "Something bad!"))
//         .Times(2);
//
//     // We allow foo.cc to call LOG(INFO) any number of times.
//     EXPECT_CALL(log, Log(INFO, HasSubstr("/foo.cc"), _))
//         .Times(AnyNumber());
//
//     Foo();  // Exercises the code under test.
//   }
class ScopedMockLog : public nglog::LogSink {
 public:
  // When a ScopedMockLog object is constructed, it starts to
  // intercept logs.
  ScopedMockLog() { AddLogSink(this); }

  // When the object is destructed, it stops intercepting logs.
  ~ScopedMockLog() override { RemoveLogSink(this); }

  // Implements the mock method:
  //
  //   void Log(LogSeverity severity, const string& file_path,
  //            const string& message);
  //
  // The second argument to Send() is the full path of the source file
  // in which the LOG() was issued.
  //
  // Note, that in a multi-threaded environment, all LOG() messages from a
  // single thread will be handled in sequence, but that cannot be guaranteed
  // for messages from different threads. In fact, if the same or multiple
  // expectations are matched on two threads concurrently, their actions will
  // be executed concurrently as well and may interleave.
  MOCK_METHOD(void, Log,
              (nglog::LogSeverity severity, const std::string& file_path,
               const std::string& message));

 private:
  // Implements the send() virtual function in class LogSink.
  // Whenever a LOG() statement is executed, this function will be
  // invoked with information presented in the LOG().
  //
  // The method argument list is long and carries much information a
  // test usually doesn't care about, so we trim the list before
  // forwarding the call to Log(), which is much easier to use in
  // tests.
  //
  // We still cannot call Log() directly, as it may invoke other LOG()
  // messages, either due to an action, or due to an error logged in
  // Google C++ Mocking Framework code, which would reenter send() while it
  // handles the current message.
  //
  // Hence, we save the message for WaitTillSent(), which runs after send() in
  // the same thread, and call Log() there. Threads that log concurrently call
  // send() concurrently as well, so the saved messages are kept per thread. A
  // message logged to this sink explicitly while it is registered as well
  // reaches send() twice before the first WaitTillSent(), so each sink keeps a
  // queue.
  void send(nglog::LogSeverity severity, const char* full_filename,
            const char* /*base_filename*/, int /*line*/,
            const LogMessageTime& /*logmsgtime*/, const char* message,
            std::size_t message_len) override {
    // We are only interested in the log severity, full file name, and
    // log message.
    PendingMessages()[this].push_back(MessageInfo{
        severity, full_filename, std::string(message, message_len)});
  }

  // Implements the WaitTillSent() virtual function in class LogSink.
  // It will be executed after send() and after the global logging lock is
  // released, so calls within it (or rather within the Log() method called
  // within) may also issue LOG() statements.
  //
  // LOG(), send(), WaitTillSent() and Log() will occur in the same thread for
  // a given log message. Messages logged directly to another sink call
  // WaitTillSent() of every registered sink as well, without a send() to this
  // one, so only a saved message is reported.
  void WaitTillSent() override {
    // First, and very importantly, we take the message being processed out
    // before calling Log(), since Log() may indirectly call send() and
    // WaitTillSent() in the same thread again.
    auto& pending_messages = PendingMessages();
    const auto pending = pending_messages.find(this);
    if (pending == pending_messages.end()) {
      return;
    }
    const MessageInfo message_info = std::move(pending->second.front());
    pending->second.pop_front();
    if (pending->second.empty()) {
      pending_messages.erase(pending);
    }
    Log(message_info.severity, message_info.file_path, message_info.message);
  }

  // All relevant information about a logged message that needs to be passed
  // from send() to WaitTillSent().
  struct MessageInfo {
    nglog::LogSeverity severity;
    std::string file_path;
    std::string message;
  };
  // Returns the messages that send() saved in the calling thread, keyed by
  // the sink that has yet to report them in the order received.
  static std::unordered_map<const ScopedMockLog*, std::deque<MessageInfo>>&
  PendingMessages() {
    thread_local std::unordered_map<const ScopedMockLog*,
                                    std::deque<MessageInfo>>
        pending_messages;
    return pending_messages;
  }
};

}  // namespace nglog_testing
}  // namespace nglog

#endif  // NGLOG_SRC_MOCK_LOG_H_
