// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_RAW_LOG_H
#define NGLOG_INTERNAL_RAW_LOG_H

#include <cstddef>

#include "ng-log/export.h"
#include "ng-log/log_severity.h"

namespace nglog {
namespace internal {

/**
 * Writes the text of a raw log message into @p buffer of @p size bytes like
 * std::snprintf does. Returns the length of the complete text, which is at
 * least @p size if the text was truncated, or a negative value on error.
 *
 * Must neither allocate memory nor acquire locks.
 */
using RawMessageFormatter = int (*)(void* context, char* buffer,
                                    std::size_t size);

/**
 * Logs a raw message reported as called from @p file:@p line to stderr. The
 * message text is produced by @p format, which is called at most once and only
 * if the message is written. Does not allocate memory or acquire locks.
 */
NGLOG_NO_EXPORT void RawLog(LogSeverity severity, const char* file, int line,
                            RawMessageFormatter format, void* context);

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_RAW_LOG_H
