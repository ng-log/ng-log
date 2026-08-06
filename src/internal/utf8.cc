// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

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

namespace {

#ifdef NGLOG_OS_WINDOWS
bool WriteUtf8ToConsole(HANDLE handle, const char* input,
                        std::size_t input_length) {
  std::wstring wide_input;
  if (!Utf8ToWide(input, input_length, &wide_input)) {
    return false;
  }

  std::size_t offset = 0;
  while (offset < wide_input.size()) {
    const std::size_t remaining = wide_input.size() - offset;
    const DWORD chunk_size = static_cast<DWORD>(std::min<std::size_t>(
        remaining,
        static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
    DWORD written = 0;
    if (!WriteConsoleW(handle, wide_input.data() + offset, chunk_size, &written,
                       nullptr) ||
        written != chunk_size) {
      return false;
    }
    offset += written;
  }
  return true;
}

bool GetNativeHandle(int file_descriptor, HANDLE* handle) {
  const intptr_t native_handle = _get_osfhandle(file_descriptor);
  if (native_handle == -1) {
    return false;
  }

  *handle = reinterpret_cast<HANDLE>(native_handle);
  return true;
}

bool IsConsoleHandle(HANDLE handle) {
  DWORD mode = 0;
  return GetConsoleMode(handle, &mode) != FALSE;
}

bool WriteBytesToFileDescriptor(int file_descriptor, const char* input,
                                std::size_t input_length) {
  std::size_t offset = 0;
  while (offset < input_length) {
    const unsigned int chunk_size = static_cast<unsigned int>(std::min(
        input_length - offset,
        static_cast<std::size_t>(std::numeric_limits<unsigned int>::max())));
    const int written = _write(file_descriptor, input + offset, chunk_size);
    if (written != static_cast<int>(chunk_size)) {
      return false;
    }
    offset += static_cast<std::size_t>(written);
  }
  return true;
}
#endif

}  // namespace

bool WriteUtf8(std::FILE* output, const char* input, std::size_t input_length) {
  if (output == nullptr || (input == nullptr && input_length != 0)) {
    errno = EINVAL;
    return false;
  }
  if (input_length == 0) {
    return true;
  }

#ifdef NGLOG_OS_WINDOWS
  HANDLE handle = nullptr;
  if (GetNativeHandle(_fileno(output), &handle)) {
    if (IsConsoleHandle(handle)) {
      if (std::fflush(output) != 0) {
        return false;
      }
      return WriteUtf8ToConsole(handle, input, input_length);
    }
    if (_setmode(_fileno(output), _O_BINARY) == -1) {
      return false;
    }
  }
#endif

  return std::fwrite(input, 1, input_length, output) == input_length;
}

bool WriteUtf8ToFileDescriptor(int file_descriptor, const char* input,
                               std::size_t input_length) {
#ifdef NGLOG_OS_WINDOWS
  if (file_descriptor < 0 || (input == nullptr && input_length != 0)) {
    errno = EINVAL;
    return false;
  }
  if (input_length == 0) {
    return true;
  }

  HANDLE handle = nullptr;
  if (GetNativeHandle(file_descriptor, &handle)) {
    if (IsConsoleHandle(handle)) {
      return WriteUtf8ToConsole(handle, input, input_length);
    }
  }
  if (_setmode(file_descriptor, _O_BINARY) == -1) {
    return false;
  }
  return WriteBytesToFileDescriptor(file_descriptor, input, input_length);
#else
  static_cast<void>(file_descriptor);
  static_cast<void>(input);
  static_cast<void>(input_length);
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
