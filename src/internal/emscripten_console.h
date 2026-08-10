// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_EMSCRIPTEN_CONSOLE_H
#define NGLOG_INTERNAL_EMSCRIPTEN_CONSOLE_H

#include "ng-log/log_severity.h"

namespace nglog {
namespace internal {

enum class EmscriptenLogLevel {
  kOut,
  kWarn,
  kError,
  kDbg,
};

EmscriptenLogLevel EmscriptenLogLevelForSeverity(LogSeverity severity);
void WriteEmscriptenLog(EmscriptenLogLevel level, const char* message);

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_EMSCRIPTEN_CONSOLE_H
