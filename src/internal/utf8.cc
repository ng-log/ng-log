// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "internal/utf8.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#ifdef NGLOG_OS_WINDOWS
#  include <fcntl.h>
#  include <io.h>
#  include <sys/stat.h>
#  include <windows.h>

// The target architecture macros that pathcch.h requires come from windows.h.
#  include <pathcch.h>
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

bool HasWindowsNamespacePrefix(const std::wstring& path) {
  constexpr wchar_t kExtendedLengthPrefix[] = L"\\\\?\\";
  constexpr wchar_t kDevicePrefix[] = L"\\\\.\\";
  constexpr std::size_t kNamespacePrefixLength =
      sizeof(kExtendedLengthPrefix) / sizeof(kExtendedLengthPrefix[0]) - 1;
  return path.size() >= kNamespacePrefixLength &&
         (path.compare(0, kNamespacePrefixLength, kExtendedLengthPrefix) == 0 ||
          path.compare(0, kNamespacePrefixLength, kDevicePrefix) == 0);
}

template <typename Character>
bool IsWindowsPathSeparator(Character character) {
  return character == static_cast<Character>('\\') ||
         character == static_cast<Character>('/');
}

// Returns whether the path names a drive root or a UNC share, including the
// namespace prefixes, and therefore does not depend on the current directory.
template <typename Character>
bool IsFullyQualifiedWindowsPath(const Character* path,
                                 std::size_t path_length) {
  constexpr std::size_t kUncPrefixLength = 2;
  constexpr std::size_t kDrivePrefixLength = 3;
  if (path_length >= kUncPrefixLength && IsWindowsPathSeparator(path[0]) &&
      IsWindowsPathSeparator(path[1])) {
    return true;
  }
  if (path_length < kDrivePrefixLength) {
    return false;
  }
  const Character drive = path[0];
  const bool is_drive_letter = (drive >= static_cast<Character>('A') &&
                                drive <= static_cast<Character>('Z')) ||
                               (drive >= static_cast<Character>('a') &&
                                drive <= static_cast<Character>('z'));
  return is_drive_letter && path[1] == static_cast<Character>(':') &&
         IsWindowsPathSeparator(path[2]);
}

// Returns an upper bound for the length of the path once Windows resolves it
// against the current directory. The MAX_PATH limit applies to this length
// rather than to the length of a relative path.
template <typename Character>
std::size_t ResolvedWindowsPathLength(const Character* path,
                                      std::size_t path_length) {
  if (IsFullyQualifiedWindowsPath(path, path_length)) {
    return path_length;
  }
  // The returned size includes the terminating null character, which
  // accounts for the separator joining both paths.
  const DWORD current_directory_size = GetCurrentDirectoryW(0, nullptr);
  return path_length + current_directory_size;
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
      if (ResolvedWindowsPathLength(wide_path.data(), wide_path.size()) <
              MAX_PATH ||
          HasWindowsNamespacePrefix(wide_path)) {
        return wide_operation(wide_path.c_str());
      }
      std::wstring prepared_path;
      if (!PrepareWindowsPath(wide_path.data(), wide_path.size(),
                              &prepared_path)) {
        return -1;
      }
      return wide_operation(prepared_path.c_str());
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
                 !HasWindowsNamespacePrefix(path, path_length) &&
                 ResolvedWindowsPathLength(path, path_length) <
                     static_cast<std::size_t>(MAX_PATH)
             ? WindowsPathKind::kNarrow
             : WindowsPathKind::kWide;
}

bool PrepareWindowsPath(const wchar_t* path, std::size_t path_length,
                        std::wstring* prepared_path) {
  if (path == nullptr || prepared_path == nullptr ||
      path_length >= PATHCCH_MAX_CCH) {
    errno = path_length >= PATHCCH_MAX_CCH ? ENAMETOOLONG : EINVAL;
    return false;
  }

  std::wstring input(path, path_length);
  if (!IsValidPath(input)) {
    return SetConversionError();
  }
  if (ResolvedWindowsPathLength(path, path_length) < MAX_PATH) {
    *prepared_path = std::move(input);
    return true;
  }

  constexpr wchar_t kExtendedPrefix[] = L"\\\\?\\";
  constexpr wchar_t kDevicePrefix[] = L"\\\\.\\";
  constexpr std::size_t kPrefixLength =
      sizeof(kExtendedPrefix) / sizeof(kExtendedPrefix[0]) - 1;
  if (HasWindowsNamespacePrefix(input)) {
    *prepared_path = std::move(input);
    return true;
  }

  std::replace(input.begin(), input.end(), L'/', L'\\');

  const DWORD current_directory_length = GetCurrentDirectoryW(0, nullptr);
  if (current_directory_length == 0) {
    return false;
  }
  std::vector<wchar_t> current_directory(current_directory_length);
  const DWORD written = GetCurrentDirectoryW(
      static_cast<DWORD>(current_directory.size()), current_directory.data());
  if (written == 0 || written >= current_directory.size()) {
    return false;
  }

  std::vector<wchar_t> combined(PATHCCH_MAX_CCH);
  const HRESULT result = PathCchCombineEx(
      combined.data(), combined.size(), current_directory.data(), input.c_str(),
      PATHCCH_ALLOW_LONG_PATHS);
  if (FAILED(result)) {
    errno = HRESULT_CODE(result) == ERROR_FILENAME_EXCED_RANGE ? ENAMETOOLONG
                                                               : EINVAL;
    return false;
  }

  std::wstring canonical_path(combined.data());
  if (canonical_path.size() < MAX_PATH ||
      canonical_path.compare(0, kPrefixLength, kExtendedPrefix) == 0 ||
      canonical_path.compare(0, kPrefixLength, kDevicePrefix) == 0) {
    *prepared_path = std::move(canonical_path);
    return true;
  }

  constexpr wchar_t kUncPrefix[] = L"\\\\";
  constexpr wchar_t kExtendedUncPrefix[] = L"\\\\?\\UNC\\";
  constexpr std::size_t kUncPrefixLength =
      sizeof(kUncPrefix) / sizeof(kUncPrefix[0]) - 1;
  constexpr std::size_t kExtendedUncPrefixLength =
      sizeof(kExtendedUncPrefix) / sizeof(kExtendedUncPrefix[0]) - 1;
  const bool is_unc =
      canonical_path.compare(0, kUncPrefixLength, kUncPrefix) == 0;
  const std::size_t prepared_length =
      canonical_path.size() +
      (is_unc ? kExtendedUncPrefixLength - kUncPrefixLength : kPrefixLength);
  if (prepared_length >= PATHCCH_MAX_CCH) {
    errno = ENAMETOOLONG;
    return false;
  }

  if (is_unc) {
    *prepared_path =
        kExtendedUncPrefix + canonical_path.substr(kUncPrefixLength);
  } else {
    *prepared_path = kExtendedPrefix + canonical_path;
  }
  return true;
}

bool Utf8ToWide(const char* input, std::size_t input_length,
                std::wstring* output) {
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
}

bool WideToUtf8(const wchar_t* input, std::size_t input_length,
                std::string* output) {
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
}

bool WideToUtf8Buffer(const wchar_t* input, std::size_t input_length,
                      char* output, std::size_t output_capacity,
                      std::size_t* output_length) {
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
}

namespace {

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

// Returns the length of the next chunk that does not split a well-formed
// UTF-8 sequence. Runs of continuation bytes longer than any valid sequence
// are ill-formed and may be split.
std::size_t Utf8ChunkLength(const char* input, std::size_t input_length) {
  constexpr std::size_t kMaximumContinuationBytes = 3;
  const std::size_t chunk_length =
      std::min(input_length, kMaximumWideChunkLength);
  std::size_t boundary = chunk_length;
  while (boundary < input_length &&
         chunk_length - boundary < kMaximumContinuationBytes &&
         IsContinuation(static_cast<std::uint8_t>(input[boundary]))) {
    --boundary;
  }
  if (boundary == 0 ||
      (boundary < input_length &&
       IsContinuation(static_cast<std::uint8_t>(input[boundary])))) {
    return chunk_length;
  }
  return boundary;
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

// Caches whether a standard stream refers to a console, which saves a system
// call per write. The cache is per thread, so the handle and its result
// cannot be observed separately while another thread replaces them.
struct ConsoleState {
  std::intptr_t handle = -1;
  bool is_console = false;
};

ConsoleState* GetConsoleState(std::FILE* output) {
  thread_local ConsoleState stdout_state;
  thread_local ConsoleState stderr_state;
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
  if (state->handle != handle_value) {
    state->is_console = IsConsoleHandle(handle);
    state->handle = handle_value;
  }
  return state->is_console;
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

}  // namespace

bool WriteUtf8AsWide(const char* input, std::size_t input_length,
                     WideWriteFunction writer, void* context) {
  if ((input == nullptr && input_length != 0) || writer == nullptr) {
    return SetConversionError();
  }
  if (input_length == 0) {
    return true;
  }

  std::array<wchar_t, kMaximumWideChunkLength> wide_buffer = {};
  std::size_t offset = 0;
  while (offset < input_length) {
    const std::size_t chunk_length =
        Utf8ChunkLength(input + offset, input_length - offset);
    if (chunk_length == 0) {
      return SetConversionError();
    }

    // Ill-formed sequences are replaced with U+FFFD instead of discarding
    // the whole message.
    constexpr DWORD kReplaceInvalidCharacters = 0;
    const int converted_length =
        MultiByteToWideChar(CP_UTF8, kReplaceInvalidCharacters, input + offset,
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
}

bool WriteWideAsChunks(const wchar_t* input, std::size_t input_length,
                       WideWriteFunction writer, void* context) {
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
    std::size_t chunk_length = std::min(remaining, kMaximumWideChunkLength);
    constexpr wchar_t kHighSurrogateMin = 0xd800;
    constexpr wchar_t kHighSurrogateMax = 0xdbff;
    const wchar_t last = input[offset + chunk_length - 1];
    // Keep surrogate pairs within a single chunk.
    if (chunk_length < remaining && last >= kHighSurrogateMin &&
        last <= kHighSurrogateMax) {
      --chunk_length;
    }
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
}

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

#endif  // NGLOG_OS_WINDOWS

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
#endif

  // Redirected output keeps the translation mode chosen by the application.
  return std::fwrite(input, 1, input_length, output) == input_length;
}

#ifdef NGLOG_OS_WINDOWS

bool WriteUtf8ToFileDescriptor(int file_descriptor, const char* input,
                               std::size_t input_length) {
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
  return WriteBytesToFileDescriptor(file_descriptor, input, input_length);
}

int OpenUtf8(const char* path, std::size_t path_length, int flags, int mode) {
  return DispatchWindowsPath(
      path, path_length,
      [flags, mode](const char* narrow_path) {
        return _open(narrow_path, flags, mode);
      },
      [flags, mode](const wchar_t* wide_path) {
        return _wopen(wide_path, flags, mode);
      });
}

int StatUtf8(const char* path, std::size_t path_length, struct stat* result) {
  return DispatchWindowsPath(
      path, path_length,
      [result](const char* narrow_path) { return ::stat(narrow_path, result); },
      [result](const wchar_t* wide_path) {
#  if defined(__MINGW32__)
        return wstat(wide_path, result);
#  else
        // The C runtime declares struct stat as a distinct type with the
        // layout of struct _stat and converts between both in its own stat().
        static_assert(sizeof(struct stat) == sizeof(struct _stat),
                      "struct stat must have the layout of struct _stat");
        return _wstat(wide_path, reinterpret_cast<struct _stat*>(result));
#  endif
      });
}

int UnlinkUtf8(const char* path, std::size_t path_length) {
  return DispatchWindowsPath(
      path, path_length,
      [](const char* narrow_path) { return _unlink(narrow_path); },
      [](const wchar_t* wide_path) { return _wunlink(wide_path); });
}

int AccessUtf8(const char* path, std::size_t path_length, int mode) {
  return DispatchWindowsPath(
      path, path_length,
      [mode](const char* narrow_path) { return _access(narrow_path, mode); },
      [mode](const wchar_t* wide_path) { return _waccess(wide_path, mode); });
}

bool GetTempPathUtf8(std::string* path) {
  return GetWindowsPath(
      [](DWORD size, wchar_t* buffer) { return GetTempPathW(size, buffer); },
      path);
}

bool GetWindowsDirectoryUtf8(std::string* path) {
  return GetWindowsPath(
      [](DWORD size, wchar_t* buffer) {
        return GetWindowsDirectoryW(buffer, size);
      },
      path);
}

bool GetComputerNameUtf8(std::string* name) {
  if (name == nullptr) {
    return SetConversionError();
  }
  constexpr std::size_t kTerminatorLength = 1;
  std::array<wchar_t, MAX_COMPUTERNAME_LENGTH + kTerminatorLength> buffer = {};
  DWORD length = static_cast<DWORD>(buffer.size());
  return GetComputerNameW(buffer.data(), &length) &&
         WideToUtf8(buffer.data(), length, name);
}

bool ForEachDirectoryEntryUtf8(const char* path, std::size_t path_length,
                               DirectoryEntryCallback callback, void* context) {
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

  const wchar_t* search_path = wide_path.c_str();
  std::wstring prepared_path;
  if (ResolvedWindowsPathLength(wide_path.data(), wide_path.size()) >=
          MAX_PATH &&
      !HasWindowsNamespacePrefix(wide_path)) {
    if (!PrepareWindowsPath(wide_path.data(), wide_path.size(),
                            &prepared_path)) {
      return false;
    }
    search_path = prepared_path.c_str();
  }
  WIN32_FIND_DATAW data;
  const HANDLE handle = FindFirstFileW(search_path, &data);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  // The callback can throw, so the search handle is released by RAII.
  struct SearchHandle {
    HANDLE handle;
    ~SearchHandle() { FindClose(handle); }
  };
  const SearchHandle search{handle};

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
  return success;
}

namespace {

bool AppendDirectoryEntry(const char* entry, std::size_t length,
                          void* context) {
  auto* const entries = static_cast<std::vector<std::string>*>(context);
  entries->emplace_back(entry, length);
  return true;
}

}  // namespace

bool ListDirectoryUtf8(const char* path, std::size_t path_length,
                       std::vector<std::string>* entries) {
  if (entries == nullptr) {
    return SetConversionError();
  }
  entries->clear();
  return ForEachDirectoryEntryUtf8(path, path_length, AppendDirectoryEntry,
                                   entries);
}

#endif  // NGLOG_OS_WINDOWS

}  // namespace internal
}  // namespace nglog
