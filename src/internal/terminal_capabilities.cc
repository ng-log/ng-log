// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "terminal_capabilities.h"

#include <cstdlib>
#include <cstring>
#include <string>

#include "config.h"
#include "ng-log/platform.h"

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

#  include <cstdint>
#  include <type_traits>
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
  const EnvironmentVariable no_color = ReadEnvironmentVariable("NO_COLOR");
  const EnvironmentVariable clicolor_force =
      ReadEnvironmentVariable("CLICOLOR_FORCE");
  const HANDLE handle =
      GetStdHandle(stream == stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
  DWORD mode = 0;

  if (handle == INVALID_HANDLE_VALUE || handle == nullptr ||
      !GetConsoleMode(handle, &mode)) {
    return ShouldColorize(
               /*is_a_tty=*/false, /*term=*/nullptr,
               no_color.present ? no_color.value.c_str() : nullptr,
               clicolor_force.present ? clicolor_force.value.c_str() : nullptr)
               ? ColorMode::kAnsi
               : ColorMode::kNone;
  }

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
  // An empty NO_COLOR does not disable color, see https://no-color.org.
  if (no_color_env != nullptr && no_color_env[0] != '\0') {
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
  return ComputeStreamColorMode(stream);
}

bool StreamSupportsColor(FILE* stream) {
  return StreamColorMode(stream) != ColorMode::kNone;
}

bool StreamSupportsHyperlinks(FILE* stream) {
  return ComputeStreamSupportsHyperlinks(stream);
}

#ifdef NGLOG_OS_WINDOWS
namespace {

constexpr WORD kForegroundBits =
    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
constexpr WORD kBackgroundBits =
    BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY;

// The colors of the classic console palette in the order of their attribute
// values, with red, green, blue and intensity as bits 2, 1, 0 and 3.
struct PaletteEntry {
  std::uint8_t red;
  std::uint8_t green;
  std::uint8_t blue;
};

constexpr PaletteEntry kConsolePalette[] = {
    {0, 0, 0},       {0, 0, 128},   {0, 128, 0},   {0, 128, 128},
    {128, 0, 0},     {128, 0, 128}, {128, 128, 0}, {192, 192, 192},
    {128, 128, 128}, {0, 0, 255},   {0, 255, 0},   {0, 255, 255},
    {255, 0, 0},     {255, 0, 255}, {255, 255, 0}, {255, 255, 255},
};

// Returns the foreground attribute bits of the palette color nearest to the
// RGB color. Ties prefer the higher palette index.
WORD NearestPaletteColor(const RgbColor& rgb) {
  const auto distance = [&rgb](const PaletteEntry& entry) {
    const int red = rgb.red - entry.red;
    const int green = rgb.green - entry.green;
    const int blue = rgb.blue - entry.blue;
    return red * red + green * green + blue * blue;
  };
  WORD nearest = 0;
  int nearest_distance = distance(kConsolePalette[0]);
  constexpr WORD kPaletteSize = std::extent<decltype(kConsolePalette)>::value;
  for (WORD index = 1; index < kPaletteSize; ++index) {
    const int candidate_distance = distance(kConsolePalette[index]);
    if (candidate_distance <= nearest_distance) {
      nearest = index;
      nearest_distance = candidate_distance;
    }
  }
  return nearest;
}

// Returns the foreground attribute bits of a color other than the default.
WORD ForegroundColorBits(const ColorValue& value) {
  if (value.kind == ColorValue::Kind::kRgb) {
    return NearestPaletteColor(value.rgb);
  }

  constexpr WORD kWhiteBits =
      FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
  switch (value.named) {
    case Color::kBlack:
      return 0;
    case Color::kRed:
      return FOREGROUND_RED;
    case Color::kGreen:
      return FOREGROUND_GREEN;
    case Color::kYellow:
      return FOREGROUND_RED | FOREGROUND_GREEN;
    case Color::kBlue:
      return FOREGROUND_BLUE;
    case Color::kMagenta:
      return FOREGROUND_RED | FOREGROUND_BLUE;
    case Color::kCyan:
      return FOREGROUND_GREEN | FOREGROUND_BLUE;
    case Color::kWhite:
    case Color::kDefault:
      return kWhiteBits;
    case Color::kGray:
      return FOREGROUND_INTENSITY;
    case Color::kBrightRed:
      return FOREGROUND_RED | FOREGROUND_INTENSITY;
    case Color::kBrightGreen:
      return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    case Color::kBrightYellow:
      return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    case Color::kBrightBlue:
      return FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    case Color::kBrightMagenta:
      return FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    case Color::kBrightCyan:
      return FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    case Color::kBrightWhite:
      return kWhiteBits | FOREGROUND_INTENSITY;
  }
  return kWhiteBits;
}

}  // namespace

WORD LegacyConsoleAttribute(ColorSpec spec, WORD current) {
  // Background bits mirror the foreground bits four positions higher.
  constexpr unsigned kBackgroundShift = 4;

  WORD attribute = spec.foreground == ColorValue(Color::kDefault)
                       ? static_cast<WORD>(current & kForegroundBits)
                       : ForegroundColorBits(spec.foreground);
  // The console cannot dim text, and other styles have no equivalent either.
  if ((spec.style & TextStyle::kBold) != TextStyle::kNone) {
    attribute |= FOREGROUND_INTENSITY;
  }

  if (spec.background == ColorValue(Color::kDefault)) {
    attribute |= current & kBackgroundBits;
  } else {
    attribute |= static_cast<WORD>(ForegroundColorBits(spec.background)
                                   << kBackgroundShift);
  }

  return attribute;
}
#endif

}  // namespace internal
}  // namespace nglog
