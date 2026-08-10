// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "terminal_capabilities.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

#include "config.h"

#ifdef NGLOG_OS_EMSCRIPTEN
#  include <emscripten.h>

// clang-format off
// Emscripten does not expose Node's process.env through C getenv() by default,
// so terminal capability detection reads those variables here without requiring
// the NODE_HOST_ENV build option.
EM_JS_DEPS(nglog_terminal_environment, "$UTF8ToString,$stringToUTF8");
EM_JS(int, nglog_get_node_environment_variable,
      (const char* name, char* value, int value_size), {
  if (typeof process === 'undefined' || process.env === undefined) {
    return 0;
  }

  const environment_value = process.env[UTF8ToString(name)];
  if (environment_value === undefined) {
    return 0;
  }

  stringToUTF8(environment_value, value, value_size);
  return 1;
});
// clang-format on
#endif

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

struct EnvironmentVariable {
  bool present;
  std::string value;
};

EnvironmentVariable ReadEnvironmentVariable(const char* name) {
  const char* const value = std::getenv(name);
  if (value != nullptr) {
    return {true, value};
  }

#ifdef NGLOG_OS_EMSCRIPTEN
  constexpr int kEnvironmentValueBufferSize = 256;
  char node_value[kEnvironmentValueBufferSize];
  if (nglog_get_node_environment_variable(name, node_value,
                                          kEnvironmentValueBufferSize) != 0) {
    return {true, node_value};
  }
#endif

  return {false, {}};
}

ColorMode ComputeStreamColorMode(FILE* stream) {
#ifdef NGLOG_OS_WINDOWS
  const HANDLE handle =
      GetStdHandle(stream == stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
  DWORD mode = 0;

  if (handle == INVALID_HANDLE_VALUE || handle == nullptr ||
      !GetConsoleMode(handle, &mode)) {
    return ColorMode::kNone;
  }

  const EnvironmentVariable no_color = ReadEnvironmentVariable("NO_COLOR");
  const EnvironmentVariable clicolor_force =
      ReadEnvironmentVariable("CLICOLOR_FORCE");
  if (!ShouldColorize(
          /*is_a_tty=*/true, /*term=*/nullptr,
          no_color.present ? no_color.value.c_str() : nullptr,
          clicolor_force.present ? clicolor_force.value.c_str() : nullptr)) {
    return ColorMode::kNone;
  }

  if (SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
    return ColorMode::kAnsi;
  }

  return ColorMode::kLegacyConsole;
#else
  const bool is_a_tty = isatty(fileno(stream)) != 0;
  const EnvironmentVariable term = ReadEnvironmentVariable("TERM");
  const EnvironmentVariable no_color = ReadEnvironmentVariable("NO_COLOR");
  const EnvironmentVariable clicolor_force =
      ReadEnvironmentVariable("CLICOLOR_FORCE");
  return ShouldColorize(
             is_a_tty, term.present ? term.value.c_str() : nullptr,
             no_color.present ? no_color.value.c_str() : nullptr,
             clicolor_force.present ? clicolor_force.value.c_str() : nullptr)
             ? ColorMode::kAnsi
             : ColorMode::kNone;
#endif
}

bool ComputeStreamSupportsHyperlinks(FILE* stream) {
  const EnvironmentVariable wt_session = ReadEnvironmentVariable("WT_SESSION");
  const EnvironmentVariable term_program =
      ReadEnvironmentVariable("TERM_PROGRAM");
  const EnvironmentVariable vte_version =
      ReadEnvironmentVariable("VTE_VERSION");
  const EnvironmentVariable konsole_version =
      ReadEnvironmentVariable("KONSOLE_VERSION");
  return ShouldEnableHyperlinks(
      ComputeStreamColorMode(stream) == ColorMode::kAnsi,
      wt_session.present ? wt_session.value.c_str() : nullptr,
      term_program.present ? term_program.value.c_str() : nullptr,
      vte_version.present ? vte_version.value.c_str() : nullptr,
      konsole_version.present ? konsole_version.value.c_str() : nullptr);
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
