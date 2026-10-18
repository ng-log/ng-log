// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_LOG_MESSAGE_ACCESS_H
#define NGLOG_INTERNAL_LOG_MESSAGE_ACCESS_H

#include "ng-log/export.h"
#include "ng-log/log_message.h"

namespace nglog {
namespace internal {

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

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_LOG_MESSAGE_ACCESS_H
