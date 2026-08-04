/* Copyright (c) 2026, The ng-log contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "internal/utf8.h"

#include <cerrno>
#include <climits>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "ng-log/platform.h"

#ifdef NGLOG_OS_WINDOWS
#  include <fcntl.h>
#  include <io.h>
#  include <sys/stat.h>
#  include <windows.h>
#endif

namespace nglog {
namespace internal {

#ifdef NGLOG_OS_WINDOWS
namespace {

constexpr std::uint8_t kAsciiMax = 0x7f;
constexpr std::uint8_t kContinuationMin = 0x80;
constexpr std::uint8_t kContinuationMax = 0xbf;
constexpr std::uint8_t kTwoByteMin = 0xc2;
constexpr std::uint8_t kTwoByteMax = 0xdf;
constexpr std::uint8_t kThreeByteMin = 0xe0;
constexpr std::uint8_t kThreeByteMax = 0xef;
constexpr std::uint8_t kFourByteMin = 0xf0;
constexpr std::uint8_t kFourByteMax = 0xf4;
constexpr std::uint8_t kThreeByteMinSecond = 0xa0;
constexpr std::uint8_t kSurrogateFirstByte = 0xed;
constexpr std::uint8_t kSurrogateMaxSecond = 0x9f;
constexpr std::uint8_t kFourByteMinSecond = 0x90;
constexpr std::uint8_t kFourByteMaxSecond = 0x8f;

bool IsContinuation(std::uint8_t value) {
  return value >= kContinuationMin && value <= kContinuationMax;
}

bool IsValidUtf8(const char* input, std::size_t input_length) {
  for (std::size_t index = 0; index < input_length; ++index) {
    const std::uint8_t first = static_cast<std::uint8_t>(input[index]);
    if (first <= kAsciiMax) {
      continue;
    }

    if (first >= kTwoByteMin && first <= kTwoByteMax) {
      if (index + 1 >= input_length ||
          !IsContinuation(static_cast<std::uint8_t>(input[index + 1]))) {
        return false;
      }
      ++index;
      continue;
    }

    if (first >= kThreeByteMin && first <= kThreeByteMax) {
      if (index + 2 >= input_length) {
        return false;
      }
      const std::uint8_t second = static_cast<std::uint8_t>(input[index + 1]);
      const bool valid_second =
          (first == kThreeByteMin && second >= kThreeByteMinSecond &&
           second <= kContinuationMax) ||
          (first == kSurrogateFirstByte && second >= kContinuationMin &&
           second <= kSurrogateMaxSecond) ||
          (first != kThreeByteMin && first != kSurrogateFirstByte &&
           IsContinuation(second));
      if (!valid_second ||
          !IsContinuation(static_cast<std::uint8_t>(input[index + 2]))) {
        return false;
      }
      index += 2;
      continue;
    }

    if (first >= kFourByteMin && first <= kFourByteMax) {
      if (index + 3 >= input_length) {
        return false;
      }
      const std::uint8_t second = static_cast<std::uint8_t>(input[index + 1]);
      const bool valid_second =
          (first == kFourByteMin && second >= kFourByteMinSecond &&
           second <= kContinuationMax) ||
          (first == kFourByteMax && second >= kContinuationMin &&
           second <= kFourByteMaxSecond) ||
          (first != kFourByteMin && first != kFourByteMax &&
           IsContinuation(second));
      if (!valid_second ||
          !IsContinuation(static_cast<std::uint8_t>(input[index + 2])) ||
          !IsContinuation(static_cast<std::uint8_t>(input[index + 3]))) {
        return false;
      }
      index += 3;
      continue;
    }

    return false;
  }
  return true;
}

bool SetConversionError() {
  errno = EINVAL;
  return false;
}

bool IsValidPath(const std::wstring& path) {
  return path.find(L'\0') == std::wstring::npos;
}

template <typename GetPathFunction>
bool GetWindowsPath(GetPathFunction get_path, std::string* path) {
  const DWORD initial_size = MAX_PATH;
  std::vector<wchar_t> buffer(initial_size);

  while (true) {
    const DWORD length =
        get_path(static_cast<DWORD>(buffer.size()), buffer.data());
    if (length == 0) {
      return false;
    }
    if (length < buffer.size()) {
      buffer.resize(length + 1);
      buffer[length] = L'\0';
      return WideToUtf8(buffer.data(), length, path);
    }
    if (buffer.size() > std::numeric_limits<DWORD>::max() / 2) {
      return SetConversionError();
    }
    buffer.resize(buffer.size() * 2);
  }
}

}  // namespace
#endif

bool Utf8ToWide(const char* input, std::size_t input_length,
                std::wstring* output) {
#ifdef NGLOG_OS_WINDOWS
  if (input == nullptr || output == nullptr) {
    return SetConversionError();
  }
  if (input_length > static_cast<std::size_t>(INT_MAX)) {
    return SetConversionError();
  }

  if (input_length == 0) {
    output->clear();
    return true;
  }
  if (!IsValidUtf8(input, input_length)) {
    return SetConversionError();
  }

  const int length =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input,
                          static_cast<int>(input_length), nullptr, 0);
  if (length <= 0) {
    return SetConversionError();
  }

  std::wstring converted(static_cast<std::size_t>(length), L'\0');
  const int converted_length = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, input, static_cast<int>(input_length),
      &converted[0], length);
  if (converted_length != length) {
    return SetConversionError();
  }

  *output = std::move(converted);
  return true;
#else
  static_cast<void>(input);
  static_cast<void>(input_length);
  static_cast<void>(output);
  return false;
#endif
}

bool WideToUtf8(const wchar_t* input, std::size_t input_length,
                std::string* output) {
#ifdef NGLOG_OS_WINDOWS
  if (input == nullptr || output == nullptr) {
    return SetConversionError();
  }
  if (input_length > static_cast<std::size_t>(INT_MAX)) {
    return SetConversionError();
  }

  if (input_length == 0) {
    output->clear();
    return true;
  }

  const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input,
                                         static_cast<int>(input_length),
                                         nullptr, 0, nullptr, nullptr);
  if (length <= 0) {
    return SetConversionError();
  }

  std::string converted(static_cast<std::size_t>(length), '\0');
  const int converted_length = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, input, static_cast<int>(input_length),
      &converted[0], length, nullptr, nullptr);
  if (converted_length != length) {
    return SetConversionError();
  }

  *output = std::move(converted);
  return true;
#else
  static_cast<void>(input);
  static_cast<void>(input_length);
  static_cast<void>(output);
  return false;
#endif
}

int OpenUtf8(const char* path, std::size_t path_length, int flags, int mode) {
#ifdef NGLOG_OS_WINDOWS
  std::wstring wide_path;
  if (!Utf8ToWide(path, path_length, &wide_path)) {
    return -1;
  }
  if (!IsValidPath(wide_path)) {
    SetConversionError();
    return -1;
  }
  return _wopen(wide_path.c_str(), flags, mode);
#else
  static_cast<void>(path);
  static_cast<void>(path_length);
  static_cast<void>(flags);
  static_cast<void>(mode);
  return -1;
#endif
}

int StatUtf8(const char* path, std::size_t path_length, struct stat* result) {
#ifdef NGLOG_OS_WINDOWS
  std::wstring wide_path;
  if (!Utf8ToWide(path, path_length, &wide_path)) {
    return -1;
  }
  if (!IsValidPath(wide_path)) {
    SetConversionError();
    return -1;
  }
#  if defined(__MINGW32__)
  return wstat(wide_path.c_str(), result);
#  else
  return _wstat(wide_path.c_str(), result);
#  endif
#else
  static_cast<void>(path);
  static_cast<void>(path_length);
  static_cast<void>(result);
  return -1;
#endif
}

int UnlinkUtf8(const char* path, std::size_t path_length) {
#ifdef NGLOG_OS_WINDOWS
  std::wstring wide_path;
  if (!Utf8ToWide(path, path_length, &wide_path)) {
    return -1;
  }
  if (!IsValidPath(wide_path)) {
    SetConversionError();
    return -1;
  }
  return _wunlink(wide_path.c_str());
#else
  static_cast<void>(path);
  static_cast<void>(path_length);
  return -1;
#endif
}

int AccessUtf8(const char* path, std::size_t path_length, int mode) {
#ifdef NGLOG_OS_WINDOWS
  std::wstring wide_path;
  if (!Utf8ToWide(path, path_length, &wide_path)) {
    return -1;
  }
  if (!IsValidPath(wide_path)) {
    SetConversionError();
    return -1;
  }
  return _waccess(wide_path.c_str(), mode);
#else
  static_cast<void>(path);
  static_cast<void>(path_length);
  static_cast<void>(mode);
  return -1;
#endif
}

bool GetTempPathUtf8(std::string* path) {
#ifdef NGLOG_OS_WINDOWS
  return GetWindowsPath(
      [](DWORD size, wchar_t* buffer) { return GetTempPathW(size, buffer); },
      path);
#else
  static_cast<void>(path);
  return false;
#endif
}

bool GetWindowsDirectoryUtf8(std::string* path) {
#ifdef NGLOG_OS_WINDOWS
  return GetWindowsPath(
      [](DWORD size, wchar_t* buffer) {
        return GetWindowsDirectoryW(buffer, size);
      },
      path);
#else
  static_cast<void>(path);
  return false;
#endif
}

bool ListDirectoryUtf8(const char* path, std::size_t path_length,
                       std::vector<std::string>* entries) {
#ifdef NGLOG_OS_WINDOWS
  if (entries == nullptr) {
    return SetConversionError();
  }

  std::wstring wide_path;
  if (!Utf8ToWide(path, path_length, &wide_path)) {
    return false;
  }
  if (wide_path.empty() || !IsValidPath(wide_path)) {
    return SetConversionError();
  }
  if (wide_path.back() != L'\\' && wide_path.back() != L'/') {
    wide_path.push_back(L'\\');
  }
  wide_path.push_back(L'*');

  WIN32_FIND_DATAW data;
  const HANDLE handle = FindFirstFileW(wide_path.c_str(), &data);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }

  entries->clear();
  bool success = true;
  do {
    std::size_t entry_size = 0;
    while (entry_size < sizeof(data.cFileName) / sizeof(data.cFileName[0]) &&
           data.cFileName[entry_size] != L'\0') {
      ++entry_size;
    }

    if ((entry_size == 1 && data.cFileName[0] == L'.') ||
        (entry_size == 2 && data.cFileName[0] == L'.' &&
         data.cFileName[1] == L'.')) {
      continue;
    }

    std::string entry;
    if (!WideToUtf8(data.cFileName, entry_size, &entry)) {
      success = false;
      break;
    }
    entries->push_back(std::move(entry));
  } while (FindNextFileW(handle, &data));

  if (success && GetLastError() != ERROR_NO_MORE_FILES) {
    success = false;
  }
  FindClose(handle);
  return success;
#else
  static_cast<void>(path);
  static_cast<void>(path_length);
  static_cast<void>(entries);
  return false;
#endif
}

}  // namespace internal
}  // namespace nglog
