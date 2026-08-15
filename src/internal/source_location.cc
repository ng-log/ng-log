// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "source_location.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <locale>

#include "config.h"
#include "ng-log/platform.h"

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

bool IsAbsolutePath(const char* path, std::size_t len) {
  const bool is_posix_absolute = len >= 1 && path[0] == '/';
  const bool is_windows_absolute =
      len >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) != 0 &&
      path[1] == ':' && (path[2] == '\\' || path[2] == '/');
  return is_posix_absolute || is_windows_absolute;
}

bool IsPathSeparator(char c) { return c == '/' || c == '\\'; }

bool IsUriPathCharacter(char c) {
  const unsigned char byte = static_cast<unsigned char>(c);
  const char locale_char = static_cast<char>(byte);
  const std::locale& locale = std::locale::classic();
  return std::isalnum(locale_char, locale) != 0 || c == '-' || c == '.' ||
         c == '_' || c == '~' || c == '/' || c == ':';
}

bool GetUriEncodedLength(const char* path, std::size_t path_length,
                         std::size_t* encoded_length) {
  constexpr std::size_t kUriUnencodedByteLength = 1;
  constexpr std::size_t kPercentEncodedByteLength = 3;
  std::size_t length = 0;
  for (std::size_t i = 0; i < path_length; ++i) {
    const bool is_path_separator = IsPathSeparator(path[i]);
    const std::size_t byte_length =
        is_path_separator || IsUriPathCharacter(path[i])
            ? kUriUnencodedByteLength
            : kPercentEncodedByteLength;
    if (length > std::numeric_limits<std::size_t>::max() - byte_length) {
      return false;
    }
    length += byte_length;
  }
  *encoded_length = length;
  return true;
}

char* AppendUriEncodedPath(const char* path, std::size_t path_length,
                           char* out) {
  constexpr char kHexDigits[] = "0123456789ABCDEF";
  constexpr unsigned kHexDigitShift = 4;
  constexpr unsigned kHexDigitMask = 0x0f;
  for (std::size_t i = 0; i < path_length; ++i) {
    const unsigned char value = static_cast<unsigned char>(path[i]);
    if (path[i] == '\\') {
      *out++ = '/';
    } else if (IsUriPathCharacter(path[i])) {
      *out++ = path[i];
    } else {
      *out++ = '%';
      *out++ = kHexDigits[value >> kHexDigitShift];
      *out++ = kHexDigits[value & kHexDigitMask];
    }
  }
  return out;
}

bool IsDigitChar(char c) {
  return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

const char* FindLastPathSeparator(const char* begin, const char* end) {
  const auto rbegin = std::make_reverse_iterator(end);
  const auto rend = std::make_reverse_iterator(begin);
  const auto it = std::find_if(rbegin, rend, IsPathSeparator);
  return it == rend ? nullptr : &*it;
}

const char* AdvancePathComponents(const char* begin, const char* end,
                                  std::size_t count) {
  const char* cursor = begin;
  for (std::size_t i = 0; i < count && cursor < end; ++i) {
    cursor = std::find_if_not(cursor, end, IsPathSeparator);
    const char* const component_start = cursor;
    cursor = std::find_if(cursor, end, IsPathSeparator);
    if (cursor == component_start) {
      break;
    }
  }
  return cursor;
}

const char* RetreatPathComponents(const char* begin, const char* end,
                                  std::size_t count) {
  const char* cursor = end;
  const auto rend = std::make_reverse_iterator(begin);
  for (std::size_t i = 0; i < count && cursor > begin; ++i) {
    const auto non_sep = std::find_if_not(std::make_reverse_iterator(cursor),
                                          rend, IsPathSeparator);
    if (non_sep == rend) {
      break;
    }
    cursor = std::find_if(non_sep, rend, IsPathSeparator).base();
  }
  return cursor;
}

}  // namespace

bool SplitFileLineSpan(const char* span, std::size_t span_length,
                       const char** path, std::size_t* path_length,
                       const char** line, std::size_t* line_length) {
  const auto rend = std::make_reverse_iterator(span);
  const auto rbegin = std::make_reverse_iterator(span + span_length);
  const auto digits_rend = std::find_if(rbegin, rend, IsDigitChar);
  if (digits_rend == rend) {
    return false;
  }

  const auto colon = std::find_if_not(digits_rend, rend, IsDigitChar);
  if (colon == rend || *colon != ':') {
    return false;
  }

  *path = span;
  *path_length = static_cast<std::size_t>(colon.base() - span) - 1;
  if (*path_length == 0) {
    return false;
  }

  *line = colon.base();
  *line_length = static_cast<std::size_t>(digits_rend.base() - *line);
  return true;
}

bool BuildFileLineUri(const char* span, std::size_t span_length,
                      const char* base_path, const char* host, char* out,
                      std::size_t out_size) {
  const char* path;
  std::size_t path_len;
  const char* line;
  std::size_t line_len;
  if (!SplitFileLineSpan(span, span_length, &path, &path_len, &line,
                         &line_len)) {
    return false;
  }

  (void)line;
  (void)line_len;
  return BuildFileUri(path, path_len, base_path, host, out, out_size);
}

bool BuildFileUri(const char* path, std::size_t path_len, const char* base_path,
                  const char* host, char* out, std::size_t out_size) {
  const bool path_is_absolute = IsAbsolutePath(path, path_len);
  const std::size_t base_len =
      (!path_is_absolute && base_path != nullptr) ? std::strlen(base_path) : 0;
  const bool need_base =
      !path_is_absolute && base_len > 0 && IsAbsolutePath(base_path, base_len);
  if (!path_is_absolute && !need_base) {
    return false;
  }

  const bool need_separator =
      need_base && !IsPathSeparator(base_path[base_len - 1]);
  constexpr char kLocalhost[] = "localhost";
  const bool have_host = host != nullptr && host[0] != '\0';
  const char* const authority = have_host ? host : kLocalhost;
  const std::size_t authority_len =
      have_host ? std::strlen(host) : sizeof(kLocalhost) - 1;
  const char* const first_segment = need_base ? base_path : path;
  const bool need_authority_separator = !IsPathSeparator(first_segment[0]);

  std::size_t encoded_base_len = 0;
  std::size_t encoded_path_len = 0;
  if ((need_base &&
       !GetUriEncodedLength(base_path, base_len, &encoded_base_len)) ||
      !GetUriEncodedLength(path, path_len, &encoded_path_len)) {
    return false;
  }

  constexpr char kPrefix[] = "file://";
  constexpr std::size_t kPrefixLen = sizeof(kPrefix) - 1;
  constexpr std::size_t kNullTerminatorLength = 1;
  constexpr std::size_t kSeparatorLength = 1;
  std::size_t needed = 0;
  const auto add_length = [&needed](std::size_t length) {
    if (needed > std::numeric_limits<std::size_t>::max() - length) {
      return false;
    }
    needed += length;
    return true;
  };
  if (!add_length(kPrefixLen) || !add_length(authority_len) ||
      (need_authority_separator && !add_length(kSeparatorLength)) ||
      (need_base && (!add_length(encoded_base_len) ||
                     (need_separator && !add_length(kSeparatorLength)))) ||
      !add_length(encoded_path_len) || !add_length(kNullTerminatorLength) ||
      needed > out_size) {
    return false;
  }

  char* cursor = std::copy_n(kPrefix, kPrefixLen, out);
  cursor = std::copy_n(authority, authority_len, cursor);
  if (need_authority_separator) {
    *cursor++ = '/';
  }
  if (need_base) {
    cursor = AppendUriEncodedPath(base_path, base_len, cursor);
    if (need_separator) {
      *cursor++ = '/';
    }
  }
  cursor = AppendUriEncodedPath(path, path_len, cursor);
  *cursor = '\0';
  return true;
}

void FormatDisplayPath(const char* path, std::size_t path_length,
                       const char* cwd, std::size_t prefix_components,
                       std::size_t suffix_components, char* out,
                       std::size_t out_size) {
  if (out_size == 0) {
    return;
  }

  if (cwd != nullptr && cwd[0] != '\0') {
    const std::size_t cwd_len = std::strlen(cwd);
    constexpr std::size_t kPosixRootLength = 1;
    constexpr std::size_t kWindowsRootLength = 3;
    const bool cwd_is_root =
        (cwd_len == kPosixRootLength && IsPathSeparator(cwd[0])) ||
        (cwd_len == kWindowsRootLength && cwd[1] == ':' &&
         IsPathSeparator(cwd[2]));

    if (path_length > cwd_len && std::equal(cwd, cwd + cwd_len, path) &&
        (cwd_is_root || IsPathSeparator(path[cwd_len]))) {
      constexpr std::size_t kNoSeparatorLength = 0;
      constexpr std::size_t kPathSeparatorLength = 1;
      const std::size_t separator_length =
          cwd_is_root ? kNoSeparatorLength : kPathSeparatorLength;
      const char* const rel = path + cwd_len + separator_length;
      const std::size_t rel_len = path_length - cwd_len - separator_length;
      const std::size_t n = std::min(rel_len, out_size - 1);
      *std::copy_n(rel, n, out) = '\0';
      return;
    }
  }

  constexpr std::size_t kCompactThreshold = 40;
  if (path_length <= kCompactThreshold) {
    const std::size_t n = std::min(path_length, out_size - 1);
    *std::copy_n(path, n, out) = '\0';
    return;
  }

  const char* const path_end = path + path_length;
  const char* const prefix_end =
      AdvancePathComponents(path, path_end, prefix_components);
  const char* const suffix_start =
      RetreatPathComponents(path, path_end, suffix_components);

  constexpr char kMiddleEllipsis[] = "/.../";
  constexpr std::size_t kMiddleEllipsisLen = sizeof(kMiddleEllipsis) - 1;
  constexpr char kLeadingEllipsis[] = ".../";

  const auto write_leading_ellipsis_and_tail =
      [out, out_size, &kLeadingEllipsis](const char* text,
                                         std::size_t text_length) {
        char* cursor = out;
        std::size_t remaining = out_size - 1;
        constexpr std::size_t kLeadingEllipsisLen =
            sizeof(kLeadingEllipsis) - 1;
        if (kLeadingEllipsisLen + 1 <= out_size) {
          cursor = std::copy_n(kLeadingEllipsis, kLeadingEllipsisLen, cursor);
          remaining -= kLeadingEllipsisLen;
        }
        const std::size_t n = std::min(text_length, remaining);
        cursor = std::copy_n(text, n, cursor);
        *cursor = '\0';
      };

  if (prefix_components == 0 || suffix_components == 0 ||
      prefix_end >= suffix_start) {
    const char* const last_sep = FindLastPathSeparator(path, path_end);
    const char* const second_last_sep =
        last_sep != nullptr ? FindLastPathSeparator(path, last_sep) : nullptr;
    const char* const tail = second_last_sep != nullptr ? second_last_sep + 1
                             : last_sep != nullptr      ? last_sep + 1
                                                        : path;
    write_leading_ellipsis_and_tail(tail,
                                    static_cast<std::size_t>(path_end - tail));
    return;
  }

  const std::size_t prefix_len = static_cast<std::size_t>(prefix_end - path);
  const std::size_t suffix_len =
      static_cast<std::size_t>(path_end - suffix_start);
  if (prefix_len + kMiddleEllipsisLen + suffix_len + 1 <= out_size) {
    char* cursor = std::copy_n(path, prefix_len, out);
    cursor = std::copy_n(kMiddleEllipsis, kMiddleEllipsisLen, cursor);
    cursor = std::copy_n(suffix_start, suffix_len, cursor);
    *cursor = '\0';
    return;
  }

  write_leading_ellipsis_and_tail(suffix_start, suffix_len);
}

void FormatSymbolizedFrame(const char* symbol, std::size_t symbol_length,
                           std::size_t file_line_offset,
                           std::size_t file_line_length, const char* cwd,
                           char* out, std::size_t out_size) {
  if (out_size == 0) {
    return;
  }

  std::size_t written = 0;
  out[0] = '\0';
  const auto append = [&written, out, out_size](const char* text,
                                                std::size_t length) {
    const std::size_t available = out_size - 1 - written;
    const std::size_t count = std::min(length, available);
    std::copy_n(text, count, out + written);
    written += count;
    out[written] = '\0';
  };

  const bool has_file_line =
      file_line_length > 0 && file_line_offset <= symbol_length &&
      file_line_length <= symbol_length - file_line_offset;
  if (!has_file_line) {
    append(symbol, symbol_length);
    return;
  }

  const char* const file_line_span = symbol + file_line_offset;
  const char* display_path;
  std::size_t display_path_length;
  const char* line;
  std::size_t line_length;
  if (SplitFileLineSpan(file_line_span, file_line_length, &display_path,
                        &display_path_length, &line, &line_length)) {
    constexpr std::size_t kDisplayPathPrefixComponents = 2;
    constexpr std::size_t kDisplayPathSuffixComponents = 2;
    char short_path[128];
    FormatDisplayPath(
        display_path, display_path_length, cwd, kDisplayPathPrefixComponents,
        kDisplayPathSuffixComponents, short_path, sizeof(short_path));
    append(short_path, std::strlen(short_path));
    append(":", 1);
    append(line, line_length);
  } else {
    append(file_line_span, file_line_length);
  }

  if (file_line_offset > 0) {
    std::size_t function_length = file_line_offset;
    if (function_length >= 2 && symbol[function_length - 2] == ' ' &&
        symbol[function_length - 1] == '(') {
      function_length -= 2;
    }
    if (function_length > 0) {
      append(" ", 1);
      append(symbol, function_length);
    }
    return;
  }

  const char* const tail = symbol + file_line_length;
  const std::size_t tail_length = symbol_length - file_line_length;
  if (tail_length > 0 && tail[0] == ' ') {
    append(" ", 1);
    append(tail + 1, tail_length - 1);
  } else {
    append(tail, tail_length);
  }
}

const std::string& CachedHostname() {
  static const std::string hostname = [] {
    char buf[256] = "";
#ifdef NGLOG_OS_WINDOWS
    char name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD len = sizeof(name) / sizeof(name[0]);
    if (GetComputerNameA(name, &len)) {
      std::copy_n(name, len, buf);
      buf[len] = '\0';
    }
#elif defined(HAVE_UNISTD_H)
    if (gethostname(buf, sizeof(buf)) != 0) {
      buf[0] = '\0';
    }
    buf[sizeof(buf) - 1] = '\0';
#endif
    return std::string(buf);
  }();
  return hostname;
}

const std::string& CachedCwd() {
  static const std::string cwd = [] {
    char buf[512] = "";
#ifdef NGLOG_OS_WINDOWS
    const DWORD len = GetCurrentDirectoryA(sizeof(buf), buf);
    if (len == 0 || len >= sizeof(buf)) {
      buf[0] = '\0';
    }
    return std::string(buf, len);
#elif defined(HAVE_UNISTD_H)
    if (getcwd(buf, sizeof(buf)) == nullptr) {
      buf[0] = '\0';
    }
    return std::string(buf);
#endif
  }();
  return cwd;
}

}  // namespace internal
}  // namespace nglog
