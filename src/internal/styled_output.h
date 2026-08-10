// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_STYLED_OUTPUT_H
#define NGLOG_INTERNAL_STYLED_OUTPUT_H

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <utility>

#include "ng-log/export.h"
#include "ng-log/internal/hyperlink.h"
#include "ng-log/internal/text_attributes.h"
#include "terminal_capabilities.h"

#ifdef NGLOG_OS_WINDOWS
#  include <windows.h>
#endif

namespace nglog {
namespace internal {

constexpr const char* const kAnsiReset = "\033[0m";
constexpr const char* const kOsc8Prefix = "\033]8;;";
constexpr const char* const kOsc8Separator = "\033\\";
constexpr const char* const kOsc8Close = "\033]8;;\033\\";

template <typename Formatter, typename Body>
inline void WithColor(Formatter& formatter, ColorSpec spec, bool enabled,
                      Body&& body) {
  if (!enabled) {
    body();
    return;
  }
  char sequence[24];
  spec.FormatAnsiSequence(sequence, sizeof(sequence));
  formatter.AppendString(sequence);
  body();
  formatter.AppendString(kAnsiReset);
}

NGLOG_NO_EXPORT void WriteRawToStderr(const char* text);

#ifdef NGLOG_OS_WINDOWS
template <typename GetInfo, typename SetAttribute, typename Body>
inline void WithLegacyConsoleAttribute(HANDLE handle, ColorSpec spec,
                                       GetInfo&& get_info,
                                       SetAttribute&& set_attribute,
                                       Body&& body) {
  CONSOLE_SCREEN_BUFFER_INFO buffer_info;
  if (!get_info(handle, &buffer_info)) {
    body();
    return;
  }
  const WORD old_attrs = buffer_info.wAttributes;
  if (!set_attribute(handle, LegacyConsoleAttribute(spec))) {
    body();
    return;
  }
  body();
  set_attribute(handle, old_attrs);
}

template <typename Body>
inline void WithLegacyConsoleAttribute(HANDLE handle, ColorSpec spec,
                                       Body&& body) {
  WithLegacyConsoleAttribute(
      handle, spec,
      [](HANDLE console_handle, PCONSOLE_SCREEN_BUFFER_INFO buffer_info) {
        return GetConsoleScreenBufferInfo(console_handle, buffer_info);
      },
      [](HANDLE console_handle, WORD attributes) {
        return SetConsoleTextAttribute(console_handle, attributes);
      },
      std::forward<Body>(body));
}

template <typename Body>
inline void WithLegacyConsoleAttribute(FILE* output, ColorSpec spec,
                                       Body&& body) {
  const HANDLE handle =
      GetStdHandle(output == stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
  WithLegacyConsoleAttribute(handle, spec, std::forward<Body>(body));
}
#endif

template <typename WriteContent>
inline void WriteColoredField(ColorSpec spec, ColorMode mode,
                              WriteContent&& write_content, const char* text,
                              std::size_t len) {
  if (len == 0) {
    return;
  }
  if (mode == ColorMode::kAnsi) {
    char sequence[24];
    spec.FormatAnsiSequence(sequence, sizeof(sequence));
    WriteRawToStderr(sequence);
    write_content(text, len);
    WriteRawToStderr(kAnsiReset);
    return;
  }
#ifdef NGLOG_OS_WINDOWS
  if (mode == ColorMode::kLegacyConsole) {
    WithLegacyConsoleAttribute(stderr, spec, [&write_content, text, len] {
      write_content(text, len);
    });
    return;
  }
#endif
  write_content(text, len);
}

template <typename Body>
inline void WrapHyperlinkRaw(const Hyperlink& hyperlink, Body&& body) {
  if (hyperlink.uri() == nullptr) {
    body();
    return;
  }
  WriteRawToStderr("\033]8;;");
  WriteRawToStderr(hyperlink.uri());
  WriteRawToStderr("\033\\");
  body();
  WriteRawToStderr("\033]8;;\033\\");
}

template <typename WriteLine, typename WriteNewline>
inline void WriteTextByLines(const char* text, std::size_t len,
                             WriteLine&& write_line,
                             WriteNewline&& write_newline) {
  while (len != 0) {
    const char* const line_end = std::find(text, text + len, '\n');
    const std::size_t line_len = static_cast<std::size_t>(line_end - text);

    if (line_len != 0) {
      write_line(text, line_len);
    }

    if (line_end == text + len) {
      return;
    }
    write_newline(line_end, 1);
    text = line_end + 1;
    len -= line_len + 1;
  }
}

template <typename WriteContent>
inline void WriteStyledField(const TextAttributes& attributes, ColorMode mode,
                             WriteContent&& write_content, const char* text,
                             std::size_t len) {
  const auto write_line = [&attributes, mode, &write_content](
                              const char* line_text, std::size_t line_len) {
    const auto write_colored = [&attributes, mode, &write_content, line_text,
                                line_len] {
      WriteColoredField(attributes.color, mode, write_content, line_text,
                        line_len);
    };
    if (mode == ColorMode::kAnsi && attributes.hyperlink.uri() != nullptr) {
      WrapHyperlinkRaw(attributes.hyperlink, write_colored);
    } else {
      write_colored();
    }
  };
  WriteTextByLines(text, len, write_line, write_content);
}

template <typename WriteContent>
inline void WriteStyledField(ColorSpec spec, ColorMode mode,
                             WriteContent&& write_content, const char* text,
                             std::size_t len) {
  WriteStyledField(TextAttributes{spec, Hyperlink()}, mode,
                   std::forward<WriteContent>(write_content), text, len);
}

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_STYLED_OUTPUT_H
