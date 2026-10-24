// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_LOG_SETUP_H
#define NGLOG_INTERNAL_LOG_SETUP_H

#include <cstddef>

#include "ng-log/export.h"
#include "ng-log/log_severity.h"

namespace nglog {
namespace internal {

// Counterparts of the setup functions that take C strings. Each takes the
// string as its first length characters, which need not be null-terminated.

NGLOG_NO_EXPORT void InitializeLogging(const char* argv0, std::size_t length);

NGLOG_NO_EXPORT void SetLogDestination(LogSeverity severity,
                                       const char* base_filename,
                                       std::size_t length);

NGLOG_NO_EXPORT void SetLogSymlink(LogSeverity severity,
                                   const char* symlink_basename,
                                   std::size_t length);

NGLOG_NO_EXPORT void SetLogFilenameExtension(const char* filename_extension,
                                             std::size_t length);

NGLOG_NO_EXPORT void SetEmailLogging(LogSeverity min_severity,
                                     const char* addresses, std::size_t length);

NGLOG_NO_EXPORT int SetVLOGLevel(const char* module_pattern, std::size_t length,
                                 int log_level);

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_LOG_SETUP_H
