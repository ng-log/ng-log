// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_LOG_MESSAGE_ACCESS_H
#define NGLOG_INTERNAL_LOG_MESSAGE_ACCESS_H

#include "ng-log/export.h"
#include "ng-log/logging.h"

namespace nglog {
namespace internal {

//! Appends ": strerror(errno) [errno]" for the errno preserved by the message.
NGLOG_NO_EXPORT void AppendErrnoDescription(LogMessage& message);

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_LOG_MESSAGE_ACCESS_H
