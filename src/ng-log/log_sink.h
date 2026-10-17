// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Declares LogSink, which receives logged messages in addition to or instead
// of the log files, and LogMessageTime, which describes when a message was
// logged.

#ifndef NGLOG_LOG_SINK_H
#define NGLOG_LOG_SINK_H

#include <chrono>
#include <cstddef>
#include <ctime>
#include <string>

#include "ng-log/export.h"
#include "ng-log/log_severity.h"

namespace nglog {

struct NGLOG_EXPORT LogMessageTime {
  LogMessageTime();
  explicit LogMessageTime(std::chrono::system_clock::time_point now);

  const std::chrono::system_clock::time_point& when() const noexcept {
    return timestamp_;
  }
  int sec() const noexcept { return tm_.tm_sec; }
  long usec() const noexcept { return usecs_.count(); }
  int(min)() const noexcept { return tm_.tm_min; }
  int hour() const noexcept { return tm_.tm_hour; }
  int day() const noexcept { return tm_.tm_mday; }
  int month() const noexcept { return tm_.tm_mon; }
  int year() const noexcept { return tm_.tm_year; }
  int dayOfWeek() const noexcept { return tm_.tm_wday; }
  int dayInYear() const noexcept { return tm_.tm_yday; }
  int dst() const noexcept { return tm_.tm_isdst; }
  std::chrono::seconds gmtoffset() const noexcept { return gmtoffset_; }
  const std::tm& tm() const noexcept { return tm_; }

 private:
  std::tm tm_{};  // Time of creation of LogMessage
  std::chrono::system_clock::time_point
      timestamp_;  // Time of creation of LogMessage in seconds
  std::chrono::microseconds usecs_;
  std::chrono::seconds gmtoffset_;
};

//
// Used to send logs to some other kind of destination
// Users should subclass LogSink and override send to do whatever they want.
// Implementations must be thread-safe because a shared instance will
// be called from whichever thread ran the LOG(XXX) line.
class NGLOG_EXPORT LogSink {
 public:
  virtual ~LogSink();

  // Sink's logging logic (message_len is such as to exclude '\n' at the end).
  // No logging mutex is held during this call, so the method may use LOG()
  // and CHECK(). A message logged here reaches this sink again, so the method
  // must not log in turn for its own messages.
  virtual void send(LogSeverity severity, const char* full_filename,
                    const char* base_filename, int line,
                    const LogMessageTime& time, const char* message,
                    std::size_t message_len) = 0;

  // Redefine this to implement waiting for
  // the sink's logging logic to complete.
  // It will be called after each send() returns,
  // but before that LogMessage exits or crashes.
  // By default this function does nothing.
  // Using this function one can implement complex logic for send()
  // that itself involves logging; and do all this w/o causing deadlocks and
  // inconsistent rearrangement of log messages.
  // E.g. if a LogSink has thread-specific actions, the send() method
  // can simply add the message to a queue and wake up another thread that
  // handles real logging while itself making some LOG() calls;
  // WaitTillSent() can be implemented to wait for that logic to complete.
  // See our unittest for an example.
  virtual void WaitTillSent();

  // Returns the normal text output of the log message.
  // Can be useful to implement send().
  static std::string ToString(LogSeverity severity, const char* file, int line,
                              const LogMessageTime& time, const char* message,
                              std::size_t message_len);
};

// Add or remove a LogSink as a consumer of logging data.  Thread-safe.
// RemoveLogSink() returns once no other thread calls the sink. Called from
// within a callback of the sink, it does not wait for threads that remove the
// sink from within its callback as well, since they would wait for each other.
NGLOG_EXPORT void AddLogSink(LogSink* destination);
NGLOG_EXPORT void RemoveLogSink(LogSink* destination);

}  // namespace nglog

#endif  // NGLOG_LOG_SINK_H
