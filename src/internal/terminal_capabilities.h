// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_TERMINAL_CAPABILITIES_H
#define NGLOG_INTERNAL_TERMINAL_CAPABILITIES_H

#include <cstdio>

#include "ng-log/export.h"
#include "ng-log/internal/color_spec.h"

#ifdef NGLOG_OS_WINDOWS
#  include <windows.h>
#endif

namespace nglog {
namespace internal {

enum class ColorMode {
  kNone,
  kAnsi,
  kLegacyConsole,
};

NGLOG_NO_EXPORT ColorMode StreamColorMode(FILE* stream);
NGLOG_NO_EXPORT bool StreamSupportsColor(FILE* stream);
NGLOG_NO_EXPORT bool StreamSupportsHyperlinks(FILE* stream);

NGLOG_NO_EXPORT bool ShouldColorize(bool is_a_tty, const char* term,
                                    const char* no_color_env,
                                    const char* clicolor_force_env);

NGLOG_NO_EXPORT bool ShouldEnableHyperlinks(bool colorize,
                                            const char* wt_session_env,
                                            const char* term_program_env,
                                            const char* vte_version_env,
                                            const char* konsole_version_env);

#ifdef NGLOG_OS_WINDOWS
NGLOG_NO_EXPORT WORD LegacyConsoleAttribute(ColorSpec spec);
#endif

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_TERMINAL_CAPABILITIES_H
