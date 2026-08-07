// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "internal/utf8.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <limits>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

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
constexpr std::size_t kWideOutputBufferLength = 1024;
constexpr std::size_t kMaximumUtf8Length = static_cast<std::size_t>(INT_MAX);

bool IsContinuation(std::uint8_t value) {
  return value >= kContinuationMin && value <= kContinuationMax;
}

struct Utf8Analysis {
  bool is_valid;
  bool is_ascii;
  bool has_null;
};

Utf8Analysis AnalyzeUtf8(const char* input, std::size_t input_length) {
  bool is_ascii = true;
  bool has_null = false;
  for (std::size_t index = 0; index < input_length; ++index) {
    const std::uint8_t first = static_cast<std::uint8_t>(input[index]);
    if (first == 0) {
      has_null = true;
    }
    if (first <= kAsciiMax) {
      continue;
    }
    is_ascii = false;

    if (first >= kTwoByteMin && first <= kTwoByteMax) {
      if (index + 1 >= input_length ||
          !IsContinuation(static_cast<std::uint8_t>(input[index + 1]))) {
        return {false, is_ascii, has_null};
      }
      ++index;
      continue;
    }

    if (first >= kThreeByteMin && first <= kThreeByteMax) {
      if (index + 2 >= input_length) {
        return {false, is_ascii, has_null};
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
        return {false, is_ascii, has_null};
      }
      index += 2;
      continue;
    }

    if (first >= kFourByteMin && first <= kFourByteMax) {
      if (index + 3 >= input_length) {
        return {false, is_ascii, has_null};
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
        return {false, is_ascii, has_null};
      }
      index += 3;
      continue;
    }

    return {false, is_ascii, has_null};
  }
  return {true, is_ascii, has_null};
}

bool IsValidUtf8(const char* input, std::size_t input_length) {
  return AnalyzeUtf8(input, input_length).is_valid;
}

bool SetConversionError() {
  errno = EINVAL;
  return false;
}

bool IsValidPath(const std::wstring& path) {
  return path.find(L'\0') == std::wstring::npos;
}

bool ConvertUtf8ToWide(const char* input, std::size_t input_length,
                       std::wstring* output) {
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
}

bool HasWindowsNamespacePrefix(const char* path, std::size_t path_length) {
  constexpr char kExtendedLengthPrefix[] = "\\\\?\\";
  constexpr char kDevicePrefix[] = "\\\\.\\";
  constexpr std::size_t kTerminatorLength = 1;
  constexpr std::size_t kNamespacePrefixLength =
      sizeof(kExtendedLengthPrefix) - kTerminatorLength;
  return path_length >= kNamespacePrefixLength &&
         (std::memcmp(path, kExtendedLengthPrefix, kNamespacePrefixLength) ==
              0 ||
          std::memcmp(path, kDevicePrefix, kNamespacePrefixLength) == 0);
}

template <typename NarrowOperation, typename WideOperation>
int DispatchWindowsPath(const char* path, std::size_t path_length,
                        NarrowOperation narrow_operation,
                        WideOperation wide_operation) {
  switch (ClassifyWindowsPath(path, path_length)) {
    case WindowsPathKind::kInvalid:
      SetConversionError();
      return -1;
    case WindowsPathKind::kNarrow: {
      std::array<char, MAX_PATH> narrow_path = {};
      std::memcpy(narrow_path.data(), path, path_length);
      narrow_path[path_length] = '\0';
      return narrow_operation(narrow_path.data());
    }
    case WindowsPathKind::kWide: {
      std::wstring wide_path;
      if (!ConvertUtf8ToWide(path, path_length, &wide_path) ||
          !IsValidPath(wide_path)) {
        SetConversionError();
        return -1;
      }
      return wide_operation(wide_path.c_str());
    }
  }
  SetConversionError();
  return -1;
}

template <typename GetPathFunction>
bool GetWindowsPath(GetPathFunction get_path, std::string* path) {
  const DWORD initial_size = MAX_PATH;
  constexpr std::size_t kBufferGrowthFactor = 2;
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
    if (buffer.size() >
        std::numeric_limits<DWORD>::max() / kBufferGrowthFactor) {
      return SetConversionError();
    }
    buffer.resize(buffer.size() * kBufferGrowthFactor);
  }
}

}  // namespace
#endif

#ifdef NGLOG_OS_WINDOWS
bool IsAscii(const char* input, std::size_t input_length) {
  if (input == nullptr) {
    return input_length == 0;
  }
  for (std::size_t index = 0; index < input_length; ++index) {
    if (static_cast<std::uint8_t>(input[index]) > kAsciiMax) {
      return false;
    }
  }
  return true;
}

WindowsPathKind ClassifyWindowsPath(const char* path, std::size_t path_length) {
  if (path == nullptr || path_length > kMaximumUtf8Length) {
    return WindowsPathKind::kInvalid;
  }

  const Utf8Analysis analysis = AnalyzeUtf8(path, path_length);
  if (!analysis.is_valid || analysis.has_null) {
    return WindowsPathKind::kInvalid;
  }

  return analysis.is_ascii &&
                 path_length < static_cast<std::size_t>(MAX_PATH) &&
                 !HasWindowsNamespacePrefix(path, path_length)
             ? WindowsPathKind::kNarrow
             : WindowsPathKind::kWide;
}
#endif

bool Utf8ToWide(const char* input, std::size_t input_length,
                std::wstring* output) {
#ifdef NGLOG_OS_WINDOWS
  if (input == nullptr || output == nullptr) {
    return SetConversionError();
  }
  if (input_length > kMaximumUtf8Length) {
    return SetConversionError();
  }
  if (!IsValidUtf8(input, input_length)) {
    return SetConversionError();
  }
  return ConvertUtf8ToWide(input, input_length, output);
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

bool WideToUtf8Buffer(const wchar_t* input, std::size_t input_length,
                      char* output, std::size_t output_capacity,
                      std::size_t* output_length) {
#ifdef NGLOG_OS_WINDOWS
  if (input == nullptr || output == nullptr || output_length == nullptr ||
      input_length > static_cast<std::size_t>(INT_MAX) ||
      output_capacity > static_cast<std::size_t>(INT_MAX)) {
    return SetConversionError();
  }
  *output_length = 0;
  if (input_length == 0) {
    return true;
  }

  const int converted_length = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, input, static_cast<int>(input_length),
      output, static_cast<int>(output_capacity), nullptr, nullptr);
  if (converted_length <= 0) {
    return SetConversionError();
  }
  *output_length = static_cast<std::size_t>(converted_length);
  return true;
#else
  static_cast<void>(input);
  static_cast<void>(input_length);
  static_cast<void>(output);
  static_cast<void>(output_capacity);
  static_cast<void>(output_length);
  return false;
#endif
}

namespace {

#ifdef NGLOG_OS_WINDOWS
template <typename Character, typename Writer>
bool WriteAll(const Character* input, std::size_t input_length,
              std::size_t maximum_chunk_length, Writer writer) {
  std::size_t offset = 0;
  while (offset < input_length) {
    const std::size_t chunk_length =
        std::min(input_length - offset, maximum_chunk_length);
    std::size_t written = 0;
    if (!writer(input + offset, chunk_length, &written) || written == 0 ||
        written > chunk_length) {
      return false;
    }
    offset += written;
  }
  return true;
}

std::size_t Utf8ChunkLength(const char* input, std::size_t input_length) {
  std::size_t chunk_length = std::min(input_length, kWideOutputBufferLength);
  while (chunk_length < input_length && chunk_length != 0 &&
         IsContinuation(static_cast<std::uint8_t>(input[chunk_length]))) {
    --chunk_length;
  }
  return chunk_length;
}

bool WriteWideToConsole(const wchar_t* input, std::size_t input_length,
                        std::size_t* written, void* context) {
  const DWORD requested = static_cast<DWORD>(input_length);
  DWORD characters_written = 0;
  const bool success =
      WriteConsoleW(static_cast<HANDLE>(context), input, requested,
                    &characters_written, nullptr) != FALSE;
  *written = characters_written;
  return success;
}

bool GetNativeHandle(int file_descriptor, HANDLE* handle) {
  const std::intptr_t native_handle = _get_osfhandle(file_descriptor);
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

struct ConsoleState {
  std::atomic<std::intptr_t> handle{-1};
  std::atomic<int> is_console{-1};
};

ConsoleState* GetConsoleState(std::FILE* output) {
  static ConsoleState stdout_state;
  static ConsoleState stderr_state;
  if (output == stdout) {
    return &stdout_state;
  }
  if (output == stderr) {
    return &stderr_state;
  }
  return nullptr;
}

bool IsConsoleStream(std::FILE* output, HANDLE handle) {
  ConsoleState* const state = GetConsoleState(output);
  if (state == nullptr) {
    return IsConsoleHandle(handle);
  }

  const std::intptr_t handle_value = reinterpret_cast<std::intptr_t>(handle);
  if (state->handle.load() == handle_value) {
    const int cached = state->is_console.load();
    if (cached >= 0) {
      return cached != 0;
    }
  }

  const bool is_console = IsConsoleHandle(handle);
  state->is_console.store(-1);
  state->handle.store(handle_value);
  state->is_console.store(is_console ? 1 : 0);
  return is_console;
}

bool SetBinaryMode(std::FILE* output) {
  const int file_descriptor = _fileno(output);
  if (file_descriptor < 0) {
    return false;
  }

  if (output == stdout || output == stderr) {
    static std::once_flag stdout_once;
    static std::once_flag stderr_once;
    static bool stdout_success = false;
    static bool stderr_success = false;
    std::once_flag* const once = output == stdout ? &stdout_once : &stderr_once;
    bool* const success = output == stdout ? &stdout_success : &stderr_success;
    std::call_once(*once, [file_descriptor, output, success] {
      *success = std::fflush(output) == 0 &&
                 _setmode(file_descriptor, _O_BINARY) != -1;
    });
    return *success;
  }

  return std::fflush(output) == 0 && _setmode(file_descriptor, _O_BINARY) != -1;
}

bool WriteBytesToHandle(HANDLE handle, const char* input,
                        std::size_t input_length) {
  constexpr std::size_t kMaximumWriteLength =
      static_cast<std::size_t>(std::numeric_limits<DWORD>::max());
  return WriteAll(input, input_length, kMaximumWriteLength,
                  [handle](const char* chunk, std::size_t chunk_length,
                           std::size_t* written) {
                    const DWORD requested = static_cast<DWORD>(chunk_length);
                    DWORD bytes_written = 0;
                    const bool success =
                        WriteFile(handle, chunk, requested, &bytes_written,
                                  nullptr) != FALSE;
                    *written = bytes_written;
                    return success;
                  });
}

bool WriteBytesToFileDescriptor(int file_descriptor, const char* input,
                                std::size_t input_length) {
  constexpr std::size_t kMaximumWriteLength =
      static_cast<std::size_t>(std::numeric_limits<unsigned int>::max());
  return WriteAll(input, input_length, kMaximumWriteLength,
                  [file_descriptor](const char* chunk, std::size_t chunk_length,
                                    std::size_t* written) {
                    const int result =
                        _write(file_descriptor, chunk,
                               static_cast<unsigned int>(chunk_length));
                    if (result < 0) {
                      *written = 0;
                      return false;
                    }
                    *written = static_cast<std::size_t>(result);
                    return true;
                  });
}

bool WriteUtf8ToConsole(HANDLE handle, const char* input,
                        std::size_t input_length) {
  if (IsAscii(input, input_length)) {
    return WriteBytesToHandle(handle, input, input_length);
  }
  return WriteUtf8AsWide(input, input_length, WriteWideToConsole, handle);
}
#endif

}  // namespace

bool WriteUtf8AsWide(const char* input, std::size_t input_length,
                     WideWriteFunction writer, void* context) {
#ifdef NGLOG_OS_WINDOWS
  if ((input == nullptr && input_length != 0) || writer == nullptr) {
    return SetConversionError();
  }
  if (input_length == 0) {
    return true;
  }
  if (!IsValidUtf8(input, input_length)) {
    return SetConversionError();
  }

  std::array<wchar_t, kWideOutputBufferLength> wide_buffer = {};
  std::size_t offset = 0;
  while (offset < input_length) {
    const std::size_t chunk_length =
        Utf8ChunkLength(input + offset, input_length - offset);
    if (chunk_length == 0) {
      return SetConversionError();
    }

    const int converted_length =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input + offset,
                            static_cast<int>(chunk_length), wide_buffer.data(),
                            static_cast<int>(wide_buffer.size()));
    if (converted_length <= 0) {
      return SetConversionError();
    }

    const std::size_t wide_length = static_cast<std::size_t>(converted_length);
    if (!WriteAll(
            wide_buffer.data(), wide_length, wide_length,
            [writer, context](const wchar_t* chunk, std::size_t chunk_size,
                              std::size_t* written) {
              return writer(chunk, chunk_size, written, context);
            })) {
      return false;
    }
    offset += chunk_length;
  }
  return true;
#else
  static_cast<void>(input);
  static_cast<void>(input_length);
  static_cast<void>(writer);
  static_cast<void>(context);
  return false;
#endif
}

bool WriteWideAsChunks(const wchar_t* input, std::size_t input_length,
                       WideWriteFunction writer, void* context) {
#ifdef NGLOG_OS_WINDOWS
  if ((input == nullptr && input_length != 0) || writer == nullptr) {
    return SetConversionError();
  }

  std::size_t offset = 0;
  while (offset < input_length) {
    while (offset < input_length && input[offset] == L'\0') {
      ++offset;
    }
    if (offset == input_length) {
      break;
    }

    const std::size_t remaining = input_length - offset;
    const std::size_t chunk_length =
        std::min(remaining, kWideOutputBufferLength);
    const wchar_t* const null_character =
        std::find(input + offset, input + offset + chunk_length, L'\0');
    const std::size_t writable_length =
        static_cast<std::size_t>(null_character - (input + offset));
    if (writable_length == 0) {
      ++offset;
      continue;
    }

    if (!WriteAll(
            input + offset, writable_length, writable_length,
            [writer, context](const wchar_t* chunk, std::size_t chunk_size,
                              std::size_t* written) {
              return writer(chunk, chunk_size, written, context);
            })) {
      return false;
    }
    offset += writable_length;
  }
  return true;
#else
  static_cast<void>(input);
  static_cast<void>(input_length);
  static_cast<void>(writer);
  static_cast<void>(context);
  return false;
#endif
}

#ifdef NGLOG_OS_WINDOWS
bool WriteSignalSafeToFileDescriptor(int file_descriptor, const char* input,
                                     std::size_t input_length) noexcept {
  if (file_descriptor < 0 || (input == nullptr && input_length != 0) ||
      input_length > static_cast<std::size_t>(INT_MAX)) {
    return false;
  }
  if (input_length == 0) {
    return true;
  }
  return _write(file_descriptor, input,
                static_cast<unsigned int>(input_length)) ==
         static_cast<int>(input_length);
}
#endif

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
  if (GetNativeHandle(_fileno(output), &handle) &&
      IsConsoleStream(output, handle)) {
    if (std::fflush(output) != 0) {
      return false;
    }
    return WriteUtf8ToConsole(handle, input, input_length);
  }

  if (!SetBinaryMode(output)) {
    return false;
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
  return DispatchWindowsPath(
      path, path_length,
      [flags, mode](const char* narrow_path) {
        return _open(narrow_path, flags, mode);
      },
      [flags, mode](const wchar_t* wide_path) {
        return _wopen(wide_path, flags, mode);
      });
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
  return DispatchWindowsPath(
      path, path_length,
      [result](const char* narrow_path) {
#  if defined(__MINGW32__)
        return ::stat(narrow_path, result);
#  else
        return _stat(narrow_path, result);
#  endif
      },
      [result](const wchar_t* wide_path) {
#  if defined(__MINGW32__)
        return wstat(wide_path, result);
#  else
        return _wstat(wide_path, result);
#  endif
      });
#else
  static_cast<void>(path);
  static_cast<void>(path_length);
  static_cast<void>(result);
  return -1;
#endif
}

int UnlinkUtf8(const char* path, std::size_t path_length) {
#ifdef NGLOG_OS_WINDOWS
  return DispatchWindowsPath(
      path, path_length,
      [](const char* narrow_path) { return _unlink(narrow_path); },
      [](const wchar_t* wide_path) { return _wunlink(wide_path); });
#else
  static_cast<void>(path);
  static_cast<void>(path_length);
  return -1;
#endif
}

int AccessUtf8(const char* path, std::size_t path_length, int mode) {
#ifdef NGLOG_OS_WINDOWS
  return DispatchWindowsPath(
      path, path_length,
      [mode](const char* narrow_path) { return _access(narrow_path, mode); },
      [mode](const wchar_t* wide_path) { return _waccess(wide_path, mode); });
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

bool GetComputerNameUtf8(std::string* name) {
#ifdef NGLOG_OS_WINDOWS
  if (name == nullptr) {
    return SetConversionError();
  }
  constexpr std::size_t kTerminatorLength = 1;
  std::array<wchar_t, MAX_COMPUTERNAME_LENGTH + kTerminatorLength> buffer = {};
  DWORD length = static_cast<DWORD>(buffer.size());
  return GetComputerNameW(buffer.data(), &length) &&
         WideToUtf8(buffer.data(), length, name);
#else
  static_cast<void>(name);
  return false;
#endif
}

bool ForEachDirectoryEntryUtf8(const char* path, std::size_t path_length,
                               DirectoryEntryCallback callback, void* context) {
#ifdef NGLOG_OS_WINDOWS
  if (callback == nullptr) {
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

  bool success = true;
  do {
    const std::size_t entry_length = std::wcslen(data.cFileName);
    if ((entry_length == 1 && data.cFileName[0] == L'.') ||
        (entry_length == 2 && data.cFileName[0] == L'.' &&
         data.cFileName[1] == L'.')) {
      continue;
    }

    std::string entry;
    if (!WideToUtf8(data.cFileName, entry_length, &entry) ||
        !callback(entry.data(), entry.size(), context)) {
      success = false;
      break;
    }
  } while (FindNextFileW(handle, &data));

  if (success && GetLastError() != ERROR_NO_MORE_FILES) {
    success = false;
  }
  FindClose(handle);
  return success;
#else
  static_cast<void>(path);
  static_cast<void>(path_length);
  static_cast<void>(callback);
  static_cast<void>(context);
  return false;
#endif
}

namespace {

#ifdef NGLOG_OS_WINDOWS
bool AppendDirectoryEntry(const char* entry, std::size_t length,
                          void* context) {
  auto* const entries = static_cast<std::vector<std::string>*>(context);
  entries->emplace_back(entry, length);
  return true;
}
#endif

}  // namespace

bool ListDirectoryUtf8(const char* path, std::size_t path_length,
                       std::vector<std::string>* entries) {
#ifdef NGLOG_OS_WINDOWS
  if (entries == nullptr) {
    return SetConversionError();
  }
  entries->clear();
  return ForEachDirectoryEntryUtf8(path, path_length, AppendDirectoryEntry,
                                   entries);
#else
  static_cast<void>(path);
  static_cast<void>(path_length);
  static_cast<void>(entries);
  return false;
#endif
}

}  // namespace internal
}  // namespace nglog
