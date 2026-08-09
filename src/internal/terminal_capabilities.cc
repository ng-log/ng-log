// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "terminal_capabilities.h"

#include <cstdlib>
#include <cstring>

#include "config.h"

#ifdef NGLOG_OS_WINDOWS
#  include <windows.h>
#else
#  ifdef HAVE_UNISTD_H
#    include <unistd.h>
#  endif
#endif

namespace nglog {
namespace internal {

namespace {

ColorMode ComputeStreamColorMode(FILE* stream) {
#ifdef NGLOG_OS_WINDOWS
  const HANDLE handle =
      GetStdHandle(stream == stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
  DWORD mode = 0;

  if (handle == INVALID_HANDLE_VALUE || handle == nullptr ||
      !GetConsoleMode(handle, &mode)) {
    return ColorMode::kNone;
  }

  if (!ShouldColorize(/*is_a_tty=*/true, /*term=*/nullptr,
                      std::getenv("NO_COLOR"), std::getenv("CLICOLOR_FORCE"))) {
    return ColorMode::kNone;
  }

  if (SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
    return ColorMode::kAnsi;
  }

  return ColorMode::kLegacyConsole;
#else
  const bool is_a_tty = isatty(fileno(stream)) != 0;
  return ShouldColorize(is_a_tty, std::getenv("TERM"), std::getenv("NO_COLOR"),
                        std::getenv("CLICOLOR_FORCE"))
             ? ColorMode::kAnsi
             : ColorMode::kNone;
#endif
}

bool ComputeStreamSupportsHyperlinks(FILE* stream) {
  return ShouldEnableHyperlinks(
      ComputeStreamColorMode(stream) == ColorMode::kAnsi,
      std::getenv("WT_SESSION"), std::getenv("TERM_PROGRAM"),
      std::getenv("VTE_VERSION"), std::getenv("KONSOLE_VERSION"));
}

}  // namespace

bool ShouldColorize(bool is_a_tty, const char* term, const char* no_color_env,
                    const char* clicolor_force_env) {
  if (no_color_env != nullptr) {
    return false;
  }

  if (clicolor_force_env != nullptr && clicolor_force_env[0] != '\0' &&
      clicolor_force_env[0] != '0') {
    return true;
  }

  if (!is_a_tty) {
    return false;
  }

  if (term != nullptr && std::strcmp(term, "dumb") == 0) {
    return false;
  }

  return true;
}

bool ShouldEnableHyperlinks(bool colorize, const char* wt_session_env,
                            const char* term_program_env,
                            const char* vte_version_env,
                            const char* konsole_version_env) {
  if (!colorize) {
    return false;
  }

  if (wt_session_env != nullptr && wt_session_env[0] != '\0') {
    return true;
  }

  if (vte_version_env != nullptr && vte_version_env[0] != '\0') {
    return true;
  }

  if (konsole_version_env != nullptr && konsole_version_env[0] != '\0') {
    return true;
  }

  if (term_program_env != nullptr &&
      (std::strcmp(term_program_env, "iTerm.app") == 0 ||
       std::strcmp(term_program_env, "WezTerm") == 0 ||
       std::strcmp(term_program_env, "Hyper") == 0)) {
    return true;
  }

  return false;
}

ColorMode StreamColorMode(FILE* stream) {
  static const ColorMode stdout_mode = ComputeStreamColorMode(stdout);
  static const ColorMode stderr_mode = ComputeStreamColorMode(stderr);
  return stream == stdout ? stdout_mode : stderr_mode;
}

bool StreamSupportsColor(FILE* stream) {
  return StreamColorMode(stream) != ColorMode::kNone;
}

bool StreamSupportsHyperlinks(FILE* stream) {
  static const bool stdout_supports = ComputeStreamSupportsHyperlinks(stdout);
  static const bool stderr_supports = ComputeStreamSupportsHyperlinks(stderr);
  return stream == stdout ? stdout_supports : stderr_supports;
}

#ifdef NGLOG_OS_WINDOWS
namespace {

WORD LegacyColorBits(const ColorValue& value, bool is_background) {
  const WORD red_bit = is_background ? BACKGROUND_RED : FOREGROUND_RED;
  const WORD green_bit = is_background ? BACKGROUND_GREEN : FOREGROUND_GREEN;
  const WORD blue_bit = is_background ? BACKGROUND_BLUE : FOREGROUND_BLUE;

  if (value.kind == ColorValue::Kind::kRgb) {
    constexpr std::uint8_t kRgbOnThreshold = 128;
    return (value.rgb.red >= kRgbOnThreshold ? red_bit : 0) |
           (value.rgb.green >= kRgbOnThreshold ? green_bit : 0) |
           (value.rgb.blue >= kRgbOnThreshold ? blue_bit : 0);
  }

  switch (value.named) {
    case Color::kBlack:
    case Color::kGray:
      return 0;
    case Color::kRed:
    case Color::kBrightRed:
      return red_bit;
    case Color::kGreen:
    case Color::kBrightGreen:
      return green_bit;
    case Color::kYellow:
    case Color::kBrightYellow:
      return red_bit | green_bit;
    case Color::kBlue:
    case Color::kBrightBlue:
      return blue_bit;
    case Color::kMagenta:
    case Color::kBrightMagenta:
      return red_bit | blue_bit;
    case Color::kCyan:
    case Color::kBrightCyan:
      return green_bit | blue_bit;
    case Color::kWhite:
    case Color::kBrightWhite:
    case Color::kDefault:
      return red_bit | green_bit | blue_bit;
  }

  return red_bit | green_bit | blue_bit;
}

bool IsIntense(const ColorValue& value) {
  if (value.kind == ColorValue::Kind::kRgb) {
    constexpr std::uint8_t kRgbBrightThreshold = 192;
    const std::uint8_t max_channel =
        std::max({value.rgb.red, value.rgb.green, value.rgb.blue});
    return max_channel >= kRgbBrightThreshold;
  }

  switch (value.named) {
    case Color::kDefault:
    case Color::kBlack:
    case Color::kRed:
    case Color::kGreen:
    case Color::kYellow:
    case Color::kBlue:
    case Color::kMagenta:
    case Color::kCyan:
    case Color::kWhite:
      return false;
    case Color::kGray:
    case Color::kBrightRed:
    case Color::kBrightGreen:
    case Color::kBrightYellow:
    case Color::kBrightBlue:
    case Color::kBrightMagenta:
    case Color::kBrightCyan:
    case Color::kBrightWhite:
      return true;
  }
  return false;
}

}  // namespace

WORD LegacyConsoleAttribute(ColorSpec spec) {
  WORD attribute = LegacyColorBits(spec.foreground, /*is_background=*/false);

  if (IsIntense(spec.foreground) || spec.style != TextStyle::kNone) {
    attribute |= FOREGROUND_INTENSITY;
  }

  if (spec.background != ColorValue(Color::kDefault)) {
    attribute |= LegacyColorBits(spec.background, /*is_background=*/true);

    if (IsIntense(spec.background)) {
      attribute |= BACKGROUND_INTENSITY;
    }
  }

  return attribute;
}
#endif

}  // namespace internal
}  // namespace nglog
