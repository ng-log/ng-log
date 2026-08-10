// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "emscripten_console.h"

#include <cstdlib>

#include "ng-log/platform.h"

#ifdef NGLOG_OS_EMSCRIPTEN
#  include <emscripten/console.h>
#endif

namespace nglog {
namespace internal {

EmscriptenLogLevel EmscriptenLogLevelForSeverity(LogSeverity severity) {
  switch (severity) {
    case NGLOG_INFO:
      return EmscriptenLogLevel::kOut;
    case NGLOG_WARNING:
      return EmscriptenLogLevel::kWarn;
    case NGLOG_ERROR:
      return EmscriptenLogLevel::kError;
    case NGLOG_FATAL:
      return EmscriptenLogLevel::kDbg;
  }

  std::abort();
}

void WriteEmscriptenLog(EmscriptenLogLevel level, const char* message) {
#ifdef NGLOG_OS_EMSCRIPTEN
  switch (level) {
    case EmscriptenLogLevel::kOut:
      emscripten_out(message);
      break;
    case EmscriptenLogLevel::kWarn:
      emscripten_console_warn(message);
      break;
    case EmscriptenLogLevel::kError:
      emscripten_err(message);
      break;
    case EmscriptenLogLevel::kDbg:
#  ifndef NDEBUG
      _emscripten_dbg(message);
#  else
      emscripten_err(message);
#  endif
      break;
  }
#else
  (void)level;
  (void)message;
#endif
}

}  // namespace internal
}  // namespace nglog
