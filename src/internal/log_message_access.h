// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_LOG_MESSAGE_ACCESS_H
#define NGLOG_INTERNAL_LOG_MESSAGE_ACCESS_H

#include <chrono>
#include <thread>

#include "ng-log/export.h"
#include "ng-log/log_message.h"
#include "stacktrace.h"

namespace nglog {
namespace internal {

/**
 * Time, thread and stack trace of a logging call, which a message that another
 * thread creates for the call reports instead of its own.
 */
struct NGLOG_NO_EXPORT LogMessageOrigin {
  std::chrono::system_clock::time_point time;
  std::thread::id thread_id;
  //! Whether --log_backtrace_at names the call site, which captured the stack.
  bool has_backtrace = false;
  CapturedStackTrace backtrace;
};

//! Captures the origin of the logging call at the file and line on this thread.
NGLOG_NO_EXPORT LogMessageOrigin CaptureLogMessageOrigin(const char* file,
                                                         int line);

/**
 * Makes the next message this thread creates while in scope report the origin
 * of a call on another thread. A null origin leaves messages unchanged.
 */
class NGLOG_NO_EXPORT LogMessageOriginScope {
 public:
  explicit LogMessageOriginScope(const LogMessageOrigin* origin) noexcept;
  LogMessageOriginScope(const LogMessageOriginScope&) = delete;
  LogMessageOriginScope& operator=(const LogMessageOriginScope&) = delete;
  ~LogMessageOriginScope();

 private:
  const LogMessageOrigin* previous_;
};

//! Grants library internals access to message state the public API hides.
struct LogMessageAccess {
  /**
   * Sets the tag that platform loggers, such as the Android log, attribute the
   * message to. A null tag selects the short name of the program.
   *
   * The tag must outlive the message.
   */
  NGLOG_NO_EXPORT static void SetTag(LogMessage& message,
                                     const char* tag) noexcept;
};

//! Appends ": strerror(errno) [errno]" for the errno preserved by the message.
NGLOG_NO_EXPORT void AppendErrnoDescription(LogMessage& message);

/**
 * Installs the function that waits until the messages other threads send are
 * sent, which FlushLogFiles() calls before flushing.
 */
NGLOG_NO_EXPORT void SetPendingMessagesWaiter(void (*waiter)()) noexcept;

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_LOG_MESSAGE_ACCESS_H
