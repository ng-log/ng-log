// Copyright (c) 2008, Google Inc.
// Copyright (c) 2026, The ng-log contributors
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Author: Shinichiro Hamaji
#include "utilities.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "internal/emscripten_console.h"
#include "internal/utf8.h"
#include "ng-log/logging.h"
#include "testing_utilities.h"

#ifdef NGLOG_OS_WINDOWS
#  include <fcntl.h>
#  include <io.h>
#  include <process.h>
#  include <sys/stat.h>
#  include <windows.h>

namespace {

std::wstring MakeExtendedPath(const std::wstring& path);

}  // namespace
#endif

#ifdef NGLOG_USE_GFLAGS
#  include <gflags/gflags.h>
using namespace GFLAGS_NAMESPACE;
#endif

using namespace nglog;

TEST(utilities, InitializeLoggingDeathTest) {
  ASSERT_DEATH(InitializeLogging("foobar"), "");
}

TEST(utilities, MakeLogFilename) {
  EXPECT_EQ(nglog::MakeLogFilename("/tmp/app[1].", "20260817-123456.42",
                                   ".foo+", true),
            "/tmp/app[1].20260817-123456.42.foo+");
  EXPECT_EQ(nglog::MakeLogFilename("/tmp/app[1].", "20260817-123456.42",
                                   ".foo+", false),
            "/tmp/app[1]..foo+");
}

TEST(utilities, MakeLogFilenameMatcher) {
  const std::regex timestamp_regex =
      nglog::tools::MakeLogFilenameMatcher("app[1].", ".foo+", true);
  EXPECT_TRUE(
      std::regex_match("app[1].20260817-123456.42.foo+", timestamp_regex));
  EXPECT_FALSE(
      std::regex_match("app11.20260817-123456.42.foo+", timestamp_regex));

  const std::regex non_timestamp_regex =
      nglog::tools::MakeLogFilenameMatcher("app[1].", ".foo+", false);
  EXPECT_TRUE(std::regex_match("app[1]..foo+", non_timestamp_regex));
  EXPECT_FALSE(
      std::regex_match("app[1].20260817-123456.42.foo+", non_timestamp_regex));
}

TEST(utilities, MakeLogFilenameMatcherRequiresBaseFilename) {
  const std::regex regex =
      nglog::tools::MakeLogFilenameMatcher("", ".foo+", true);
  EXPECT_FALSE(std::regex_match("20260817-123456.42.foo+", regex));
}

TEST(EmscriptenConsole, MapsSeverityToConsoleLevel) {
  EXPECT_EQ(internal::EmscriptenLogLevelForSeverity(NGLOG_INFO),
            internal::EmscriptenLogLevel::kOut);
  EXPECT_EQ(internal::EmscriptenLogLevelForSeverity(NGLOG_WARNING),
            internal::EmscriptenLogLevel::kWarn);
  EXPECT_EQ(internal::EmscriptenLogLevelForSeverity(NGLOG_ERROR),
            internal::EmscriptenLogLevel::kError);
  EXPECT_EQ(internal::EmscriptenLogLevelForSeverity(NGLOG_FATAL),
            internal::EmscriptenLogLevel::kDbg);
}

TEST(utilities, TrimTrailingCRLFRemovesTrailingNewlines) {
  EXPECT_EQ(nglog::TrimTrailingCRLF("message\r\n"), "message");
  EXPECT_EQ(nglog::TrimTrailingCRLF("message\n"), "message");
  EXPECT_EQ(nglog::TrimTrailingCRLF("message"), "message");
  EXPECT_EQ(nglog::TrimTrailingCRLF("\r\n"), "");
}

TEST(utilities, TrimTrailingCharacters) {
  constexpr char delimiters[] = {' ', '\t'};

  EXPECT_EQ(nglog::TrimTrailingCharacters("message \t", delimiters), "message");
  EXPECT_EQ(nglog::TrimTrailingCharacters("message \t", delimiters,
                                          sizeof(delimiters)),
            "message");
  EXPECT_EQ(nglog::TrimTrailingCharacters("message", " \t"), "message");
  EXPECT_EQ(nglog::TrimTrailingCharacters(" \t", delimiters), "");
}

#ifdef NGLOG_OS_WINDOWS
TEST(WindowsUtf8Path, ConvertsValidLengthDelimitedUtf8) {
  constexpr std::array<char, 5> kUtf8 = {'c', 'a', 'f', '\xC3', '\xA9'};
  std::wstring wide;
  ASSERT_TRUE(nglog::internal::Utf8ToWide(kUtf8.data(), kUtf8.size(), &wide));
  EXPECT_EQ(wide, L"caf\u00E9");

  std::string round_trip;
  ASSERT_TRUE(
      nglog::internal::WideToUtf8(wide.data(), wide.size(), &round_trip));
  EXPECT_EQ(round_trip, std::string(kUtf8.data(), kUtf8.size()));
}

TEST(WindowsUtf8Path, ConvertsWideTextIntoCallerBuffer) {
  constexpr wchar_t kWide[] = {L'c', L'a', L'f', L'\0', L'\u00E9'};
  constexpr char kExpected[] = {'c', 'a', 'f', '\0', '\xC3', '\xA9'};
  std::array<char, sizeof(kExpected)> output = {};
  std::size_t output_length = 0;

  ASSERT_TRUE(nglog::internal::WideToUtf8Buffer(
      kWide, sizeof(kWide) / sizeof(kWide[0]), output.data(), output.size(),
      &output_length));
  ASSERT_EQ(output_length, sizeof(kExpected));
  EXPECT_EQ(std::memcmp(output.data(), kExpected, sizeof(kExpected)), 0);
}

TEST(WindowsUtf8Path, RejectsSmallCallerBuffer) {
  constexpr wchar_t kWide[] = L"caf\u00E9";
  constexpr std::size_t kSmallBufferLength = 4;
  constexpr std::size_t kTerminatorLength = 1;
  std::array<char, kSmallBufferLength> output = {};
  std::size_t output_length = 0;

  errno = 0;
  EXPECT_FALSE(nglog::internal::WideToUtf8Buffer(
      kWide, sizeof(kWide) / sizeof(kWide[0]) - kTerminatorLength,
      output.data(), output.size(), &output_length));
  EXPECT_EQ(errno, EINVAL);
  EXPECT_EQ(output_length, 0U);
}

TEST(WindowsUtf8Path, RejectsEmbeddedNull) {
  constexpr std::array<char, 4> kPathWithNull = {'l', 'o', '\0', 'g'};
  errno = 0;
  EXPECT_EQ(nglog::internal::AccessUtf8(kPathWithNull.data(),
                                        kPathWithNull.size(), 0),
            -1);
  EXPECT_EQ(errno, EINVAL);
}

TEST(WindowsUtf8Path, OperatesOnLengthDelimitedUtf8Path) {
  const std::string path =
      "nglog_unicode_path_" + std::to_string(_getpid()) + "_\xE2\x82\xAC.tmp";
  const std::vector<char> length_delimited_path(path.begin(), path.end());

  const int file_descriptor = nglog::internal::OpenUtf8(
      length_delimited_path.data(), length_delimited_path.size(),
      _O_CREAT | _O_TRUNC | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
  ASSERT_NE(file_descriptor, -1);
  ASSERT_EQ(_close(file_descriptor), 0);

  struct stat file_status = {};
  EXPECT_EQ(
      nglog::internal::StatUtf8(length_delimited_path.data(),
                                length_delimited_path.size(), &file_status),
      0);
  EXPECT_EQ(nglog::internal::AccessUtf8(length_delimited_path.data(),
                                        length_delimited_path.size(), 0),
            0);
  EXPECT_EQ(nglog::internal::UnlinkUtf8(length_delimited_path.data(),
                                        length_delimited_path.size()),
            0);
}

TEST(WindowsUtf8Path, GetsComputerNameAsUtf8) {
  std::string computer_name;
  ASSERT_TRUE(nglog::internal::GetComputerNameUtf8(&computer_name));

  constexpr std::size_t kTerminatorLength = 1;
  std::array<wchar_t, MAX_COMPUTERNAME_LENGTH + kTerminatorLength> wide_name =
      {};
  DWORD wide_name_length = static_cast<DWORD>(wide_name.size());
  ASSERT_TRUE(GetComputerNameW(wide_name.data(), &wide_name_length));
  std::string expected;
  ASSERT_TRUE(nglog::internal::WideToUtf8(wide_name.data(), wide_name_length,
                                          &expected));
  EXPECT_EQ(computer_name, expected);
}

TEST(WindowsUtf8Path, GetsSystemPathsAsUtf8) {
  std::string temporary_path;
  ASSERT_TRUE(nglog::internal::GetTempPathUtf8(&temporary_path));
  EXPECT_FALSE(temporary_path.empty());

  std::string windows_path;
  ASSERT_TRUE(nglog::internal::GetWindowsDirectoryUtf8(&windows_path));
  EXPECT_FALSE(windows_path.empty());
}

TEST(WindowsUtf8Path, ListsUnicodeDirectoryEntries) {
  std::string temporary_path;
  ASSERT_TRUE(nglog::internal::GetTempPathUtf8(&temporary_path));
  temporary_path += "nglog_directory_\xE2\x82\xAC_" +
                    std::to_string(_getpid()) + "_" +
                    std::to_string(GetTickCount64());

  std::wstring wide_directory;
  ASSERT_TRUE(nglog::internal::Utf8ToWide(
      temporary_path.data(), temporary_path.size(), &wide_directory));
  ASSERT_TRUE(CreateDirectoryW(wide_directory.c_str(), nullptr));

  const std::string filename = "entry_\xE2\x82\xAC.txt";
  const std::string path = temporary_path + "\\" + filename;
  const int file_descriptor = nglog::internal::OpenUtf8(
      path.data(), path.size(), _O_CREAT | _O_WRONLY | _O_BINARY,
      _S_IREAD | _S_IWRITE);
  ASSERT_NE(file_descriptor, -1);
  ASSERT_EQ(_close(file_descriptor), 0);

  std::vector<std::string> entries;
  ASSERT_TRUE(nglog::internal::ListDirectoryUtf8(
      temporary_path.data(), temporary_path.size(), &entries));
  EXPECT_EQ(entries, std::vector<std::string>{filename});

  EXPECT_EQ(nglog::internal::UnlinkUtf8(path.data(), path.size()), 0);
  EXPECT_TRUE(RemoveDirectoryW(wide_directory.c_str()));
}

TEST(WindowsUtf8Path, RejectsInvalidUtf8DirectoryPath) {
  constexpr char kInvalidUtf8[] = {'\xC3', '\x28'};
  std::vector<std::string> entries;
  errno = 0;
  EXPECT_FALSE(nglog::internal::ListDirectoryUtf8(
      kInvalidUtf8, sizeof(kInvalidUtf8), &entries));
  EXPECT_EQ(errno, EINVAL);
}

TEST(WindowsUtf8Path, ClassifiesInvalidUtf8Path) {
  constexpr char kInvalidUtf8[] = {'\xC3', '\x28'};

  EXPECT_EQ(
      nglog::internal::ClassifyWindowsPath(kInvalidUtf8, sizeof(kInvalidUtf8)),
      nglog::internal::WindowsPathKind::kInvalid);
}
#endif

TEST(Utf8Output, PreservesRedirectedBytes) {
  const std::string message = "caf\xC3\xA9 \xE2\x98\x83";
  std::FILE* file = std::tmpfile();
  ASSERT_NE(file, nullptr);

  EXPECT_TRUE(nglog::internal::WriteUtf8(file, message.data(), message.size()));
  EXPECT_EQ(std::ftell(file), static_cast<long>(message.size()));
  ASSERT_EQ(std::fflush(file), 0);
  ASSERT_EQ(std::fseek(file, 0, SEEK_SET), 0);

  std::string output(message.size(), '\0');
  ASSERT_EQ(std::fread(output.data(), 1, output.size(), file), output.size());
  EXPECT_EQ(output, message);
  std::fclose(file);
}

TEST(Utf8Output, PreservesLengthDelimitedBytes) {
  const char message[] = {'a', '\0', '\xFF', 'b'};
  std::FILE* file = std::tmpfile();
  ASSERT_NE(file, nullptr);

  EXPECT_TRUE(nglog::internal::WriteUtf8(file, message, sizeof(message)));
  ASSERT_EQ(std::fflush(file), 0);
  ASSERT_EQ(std::fseek(file, 0, SEEK_SET), 0);

  char output[sizeof(message)] = {};
  ASSERT_EQ(std::fread(output, 1, sizeof(output), file), sizeof(output));
  EXPECT_EQ(std::memcmp(output, message, sizeof(message)), 0);
  std::fclose(file);
}

#ifdef NGLOG_OS_WINDOWS
TEST(Utf8Output, PreservesRedirectedTextModeBytes) {
  std::FILE* file = std::tmpfile();
  ASSERT_NE(file, nullptr);
  ASSERT_NE(_setmode(_fileno(file), _O_TEXT), -1);

  constexpr char kMessage[] = {'a', '\n', 'b'};
  ASSERT_TRUE(nglog::internal::WriteUtf8(file, kMessage, sizeof(kMessage)));
  ASSERT_EQ(std::fflush(file), 0);
  ASSERT_EQ(std::fseek(file, 0, SEEK_SET), 0);

  char output[sizeof(kMessage)] = {};
  ASSERT_EQ(std::fread(output, 1, sizeof(output), file), sizeof(output));
  EXPECT_EQ(std::memcmp(output, kMessage, sizeof(kMessage)), 0);
  std::fclose(file);
}

TEST(Utf8Output, WritesSignalSafeLengthDelimitedBytes) {
  std::FILE* file = std::tmpfile();
  ASSERT_NE(file, nullptr);

  constexpr char kMessage[] = {'a', '\0', 'b'};
  ASSERT_TRUE(nglog::internal::WriteSignalSafeToFileDescriptor(
      _fileno(file), kMessage, sizeof(kMessage)));
  ASSERT_EQ(std::fflush(file), 0);
  ASSERT_EQ(std::fseek(file, 0, SEEK_SET), 0);

  char output[sizeof(kMessage)] = {};
  ASSERT_EQ(std::fread(output, 1, sizeof(output), file), sizeof(output));
  EXPECT_EQ(std::memcmp(output, kMessage, sizeof(kMessage)), 0);
  std::fclose(file);
}

TEST(WindowsUtf8Path, ClassifiesLengthDelimitedPaths) {
  constexpr std::array<char, 3> kAsciiPath = {'l', 'o', 'g'};
  constexpr std::array<char, 4> kPathWithNull = {'l', 'o', '\0', 'g'};
  constexpr char kUtf8Path[] = "caf\xC3\xA9";
  constexpr char kExtendedPath[] = "\\\\?\\C:\\log";
  constexpr char kExtendedUncPath[] = "\\\\?\\UNC\\server\\share";
  constexpr char kDevicePath[] = "\\\\.\\NUL";
  constexpr std::size_t kTerminatorLength = 1;
  constexpr std::size_t kMaximumPathLength = static_cast<std::size_t>(MAX_PATH);
  const std::string largest_narrow_path(kMaximumPathLength - kTerminatorLength,
                                        'a');
  const std::string long_ascii_path(kMaximumPathLength, 'a');

  EXPECT_EQ(nglog::internal::ClassifyWindowsPath(kAsciiPath.data(),
                                                 kAsciiPath.size()),
            nglog::internal::WindowsPathKind::kNarrow);
  EXPECT_EQ(nglog::internal::ClassifyWindowsPath(kPathWithNull.data(),
                                                 kPathWithNull.size()),
            nglog::internal::WindowsPathKind::kInvalid);
  EXPECT_EQ(nglog::internal::ClassifyWindowsPath(
                kUtf8Path, sizeof(kUtf8Path) - kTerminatorLength),
            nglog::internal::WindowsPathKind::kWide);
  EXPECT_EQ(nglog::internal::ClassifyWindowsPath(
                kExtendedPath, sizeof(kExtendedPath) - kTerminatorLength),
            nglog::internal::WindowsPathKind::kWide);
  EXPECT_EQ(nglog::internal::ClassifyWindowsPath(
                kExtendedUncPath, sizeof(kExtendedUncPath) - kTerminatorLength),
            nglog::internal::WindowsPathKind::kWide);
  EXPECT_EQ(nglog::internal::ClassifyWindowsPath(
                kDevicePath, sizeof(kDevicePath) - kTerminatorLength),
            nglog::internal::WindowsPathKind::kWide);
  EXPECT_EQ(nglog::internal::ClassifyWindowsPath(largest_narrow_path.data(),
                                                 largest_narrow_path.size()),
            nglog::internal::WindowsPathKind::kNarrow);
  EXPECT_EQ(nglog::internal::ClassifyWindowsPath(long_ascii_path.data(),
                                                 long_ascii_path.size()),
            nglog::internal::WindowsPathKind::kWide);
}

TEST(WindowsUtf8Path, OperatesOnLengthDelimitedAsciiPath) {
  const std::string path =
      "nglog_ascii_path_" + std::to_string(_getpid()) + ".tmp";
  const std::vector<char> length_delimited_path(path.begin(), path.end());

  const int file_descriptor = nglog::internal::OpenUtf8(
      length_delimited_path.data(), length_delimited_path.size(),
      _O_CREAT | _O_TRUNC | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
  ASSERT_NE(file_descriptor, -1);
  ASSERT_EQ(_close(file_descriptor), 0);

  struct stat file_status = {};
  EXPECT_EQ(
      nglog::internal::StatUtf8(length_delimited_path.data(),
                                length_delimited_path.size(), &file_status),
      0);
  EXPECT_EQ(nglog::internal::AccessUtf8(length_delimited_path.data(),
                                        length_delimited_path.size(), 0),
            0);
  EXPECT_EQ(nglog::internal::UnlinkUtf8(length_delimited_path.data(),
                                        length_delimited_path.size()),
            0);
}

TEST(WindowsUtf8Path, OperatesOnExtendedLengthPath) {
  constexpr DWORD kRequiredSizeQuery = 0;
  constexpr int kExistenceMode = 0;
  constexpr std::size_t kTerminatorLength = 1;
  const DWORD current_directory_buffer_length =
      GetCurrentDirectoryW(kRequiredSizeQuery, nullptr);
  ASSERT_NE(current_directory_buffer_length, 0U);

  std::vector<wchar_t> current_directory(current_directory_buffer_length);
  const DWORD current_directory_length = GetCurrentDirectoryW(
      current_directory_buffer_length, current_directory.data());
  ASSERT_EQ(current_directory_length,
            current_directory_buffer_length - kTerminatorLength);

  const std::wstring current_directory_path(current_directory.data(),
                                            current_directory_length);
  std::wstring wide_path = MakeExtendedPath(current_directory_path);
  if (wide_path.back() != L'\\') {
    wide_path.push_back(L'\\');
  }
  wide_path += L"nglog_extended_path_" + std::to_wstring(_getpid()) + L".tmp";

  std::string path;
  ASSERT_TRUE(
      nglog::internal::WideToUtf8(wide_path.data(), wide_path.size(), &path));
  const int file_descriptor = nglog::internal::OpenUtf8(
      path.data(), path.size(), _O_CREAT | _O_TRUNC | _O_WRONLY | _O_BINARY,
      _S_IREAD | _S_IWRITE);
  ASSERT_NE(file_descriptor, -1);
  ASSERT_EQ(_close(file_descriptor), 0);

  struct stat file_status = {};
  EXPECT_EQ(nglog::internal::StatUtf8(path.data(), path.size(), &file_status),
            0);
  EXPECT_EQ(
      nglog::internal::AccessUtf8(path.data(), path.size(), kExistenceMode), 0);
  EXPECT_EQ(nglog::internal::UnlinkUtf8(path.data(), path.size()), 0);
}

TEST(WindowsUtf8Path, MakesExtendedPathForDriveAndUncDirectories) {
  EXPECT_EQ(MakeExtendedPath(L"C:\\work"), L"\\\\?\\C:\\work");
  EXPECT_EQ(MakeExtendedPath(L"\\\\server\\share\\work"),
            L"\\\\?\\UNC\\server\\share\\work");
  EXPECT_EQ(MakeExtendedPath(L"\\\\?\\C:\\work"), L"\\\\?\\C:\\work");
  EXPECT_EQ(MakeExtendedPath(L"\\\\?\\UNC\\server\\share\\work"),
            L"\\\\?\\UNC\\server\\share\\work");
}

TEST(WindowsUtf8Path, OpensDeviceNamespacePath) {
  constexpr char kNullDevice[] = "\\\\.\\NUL";
  constexpr int kUnusedMode = 0;
  constexpr std::size_t kTerminatorLength = 1;
  const int file_descriptor = nglog::internal::OpenUtf8(
      kNullDevice, sizeof(kNullDevice) - kTerminatorLength,
      _O_WRONLY | _O_BINARY, kUnusedMode);
  ASSERT_NE(file_descriptor, -1);
  EXPECT_EQ(_close(file_descriptor), 0);
}

TEST(Utf8Output, ClassifiesAsciiWithoutTerminator) {
  constexpr std::array<char, 4> kAsciiMessage = {'l', 'o', 'g', '\0'};
  constexpr char kUtf8Message[] = "caf\xC3\xA9";

  EXPECT_TRUE(
      nglog::internal::IsAscii(kAsciiMessage.data(), kAsciiMessage.size()));
  EXPECT_FALSE(
      nglog::internal::IsAscii(kUtf8Message, sizeof(kUtf8Message) - 1));
}

TEST(Utf8Output, WritesAsciiToConsoleFileDescriptor) {
  HANDLE console = CreateConsoleScreenBuffer(
      GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
      CONSOLE_TEXTMODE_BUFFER, nullptr);
  if (console == INVALID_HANDLE_VALUE) {
    GTEST_SKIP() << "Windows console is unavailable";
  }

  const int file_descriptor = _open_osfhandle(
      reinterpret_cast<std::intptr_t>(console), _O_WRONLY | _O_BINARY);
  ASSERT_NE(file_descriptor, -1);

  constexpr std::array<char, 3> kMessage = {'l', 'o', 'g'};
  ASSERT_TRUE(nglog::internal::WriteUtf8ToFileDescriptor(
      file_descriptor, kMessage.data(), kMessage.size()));

  std::array<wchar_t, kMessage.size()> output = {};
  constexpr COORD kPosition = {0, 0};
  DWORD characters_read = 0;
  ASSERT_TRUE(ReadConsoleOutputCharacterW(console, output.data(),
                                          static_cast<DWORD>(output.size()),
                                          kPosition, &characters_read));
  ASSERT_EQ(characters_read, output.size());
  EXPECT_EQ(std::wstring(L"log"), std::wstring(output.data(), characters_read));
  _close(file_descriptor);
}

TEST(Utf8Output, WritesAsciiToConsoleFile) {
  HANDLE console = CreateConsoleScreenBuffer(
      GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
      CONSOLE_TEXTMODE_BUFFER, nullptr);
  if (console == INVALID_HANDLE_VALUE) {
    GTEST_SKIP() << "Windows console is unavailable";
  }

  const int file_descriptor = _open_osfhandle(
      reinterpret_cast<std::intptr_t>(console), _O_WRONLY | _O_BINARY);
  ASSERT_NE(file_descriptor, -1);
  std::FILE* stream = _fdopen(file_descriptor, "wb");
  ASSERT_NE(stream, nullptr);

  constexpr std::size_t kMessageLength = 3;
  constexpr std::array<char, kMessageLength> kMessage = {'l', 'o', 'g'};
  ASSERT_TRUE(
      nglog::internal::WriteUtf8(stream, kMessage.data(), kMessage.size()));

  std::array<wchar_t, kMessage.size()> output = {};
  constexpr COORD kPosition = {0, 0};
  DWORD characters_read = 0;
  ASSERT_TRUE(ReadConsoleOutputCharacterW(console, output.data(),
                                          static_cast<DWORD>(output.size()),
                                          kPosition, &characters_read));
  ASSERT_EQ(characters_read, output.size());
  EXPECT_EQ(std::wstring(L"log"), std::wstring(output.data(), characters_read));
  std::fclose(stream);
}

TEST(Utf8Output, PreservesRedirectedFileDescriptorBytes) {
  constexpr std::size_t kMessageLength = 4;
  constexpr std::size_t kPipeEndCount = 2;
  constexpr unsigned int kPipeBufferLength = 4096;
  std::array<int, kPipeEndCount> file_descriptors = {};
  ASSERT_EQ(_pipe(file_descriptors.data(), kPipeBufferLength, _O_BINARY), 0);

  constexpr std::array<char, kMessageLength> kMessage = {'a', '\0', '\xFF',
                                                         'b'};
  ASSERT_TRUE(nglog::internal::WriteUtf8ToFileDescriptor(
      file_descriptors[1], kMessage.data(), kMessage.size()));
  ASSERT_EQ(_close(file_descriptors[1]), 0);

  std::array<char, kMessage.size()> output = {};
  ASSERT_EQ(_read(file_descriptors[0], output.data(),
                  static_cast<unsigned int>(output.size())),
            static_cast<int>(output.size()));
  EXPECT_EQ(output, kMessage);
  EXPECT_EQ(_close(file_descriptors[0]), 0);
}

TEST(Utf8Output, PreservesAppendModeFileDescriptor) {
  constexpr int kFilePermissions = _S_IREAD | _S_IWRITE;
  constexpr int kCreateFlags = _O_CREAT | _O_TRUNC | _O_WRONLY | _O_BINARY;
  constexpr int kAppendFlags = _O_WRONLY | _O_APPEND | _O_BINARY;
  constexpr int kReadFlags = _O_RDONLY | _O_BINARY;
  constexpr char kInitialMessage[] = "first";
  constexpr char kAppendedMessage[] = "second";
  constexpr std::size_t kExpectedLength =
      sizeof(kInitialMessage) + sizeof(kAppendedMessage) - 2;
  const std::string path =
      "nglog_append_path_" + std::to_string(_getpid()) + ".tmp";

  const int initial_file_descriptor =
      _open(path.c_str(), kCreateFlags, kFilePermissions);
  ASSERT_NE(initial_file_descriptor, -1);
  ASSERT_EQ(_write(initial_file_descriptor, kInitialMessage,
                   sizeof(kInitialMessage) - 1),
            static_cast<int>(sizeof(kInitialMessage) - 1));
  ASSERT_EQ(_close(initial_file_descriptor), 0);

  const int append_file_descriptor =
      _open(path.c_str(), kAppendFlags, kFilePermissions);
  ASSERT_NE(append_file_descriptor, -1);
  ASSERT_EQ(_lseek(append_file_descriptor, 0, SEEK_SET), 0);
  ASSERT_TRUE(nglog::internal::WriteUtf8ToFileDescriptor(
      append_file_descriptor, kAppendedMessage, sizeof(kAppendedMessage) - 1));
  ASSERT_EQ(_close(append_file_descriptor), 0);

  const int read_file_descriptor = _open(path.c_str(), kReadFlags);
  ASSERT_NE(read_file_descriptor, -1);
  std::array<char, kExpectedLength> output = {};
  ASSERT_EQ(_read(read_file_descriptor, output.data(), output.size()),
            static_cast<int>(output.size()));
  EXPECT_EQ(std::string(output.data(), output.size()), "firstsecond");
  ASSERT_EQ(_close(read_file_descriptor), 0);
  ASSERT_EQ(_unlink(path.c_str()), 0);
}

TEST(Utf8Output, PreservesRedirectedTextModeFileBytes) {
  std::FILE* stream = std::tmpfile();
  ASSERT_NE(stream, nullptr);
  ASSERT_NE(_setmode(_fileno(stream), _O_TEXT), -1);

  constexpr std::size_t kCarriageReturnLength = 1;
  constexpr std::size_t kMessageLength = 3;
  constexpr std::array<char, kMessageLength> kMessage = {'a', '\n', 'b'};
  ASSERT_TRUE(
      nglog::internal::WriteUtf8(stream, kMessage.data(), kMessage.size()));
  ASSERT_EQ(std::fflush(stream), 0);

  const std::intptr_t native_handle = _get_osfhandle(_fileno(stream));
  ASSERT_NE(native_handle, -1);
  const HANDLE handle = reinterpret_cast<HANDLE>(native_handle);
  LARGE_INTEGER position = {};
  ASSERT_TRUE(SetFilePointerEx(handle, position, nullptr, FILE_BEGIN));

  constexpr std::size_t kMaximumTranslatedBytes =
      kMessage.size() + kCarriageReturnLength;
  std::array<char, kMaximumTranslatedBytes> output = {};
  DWORD bytes_read = 0;
  ASSERT_TRUE(ReadFile(handle, output.data(), static_cast<DWORD>(output.size()),
                       &bytes_read, nullptr));
  ASSERT_EQ(bytes_read, kMessage.size());
  EXPECT_EQ(std::memcmp(output.data(), kMessage.data(), kMessage.size()), 0);
  std::fclose(stream);
}

namespace {

std::wstring MakeExtendedPath(const std::wstring& path) {
  constexpr wchar_t kExtendedPrefix[] = L"\\\\?\\";
  constexpr wchar_t kUncPrefix[] = L"\\\\";
  constexpr wchar_t kExtendedUncPrefix[] = L"\\\\?\\UNC\\";
  constexpr std::size_t kTerminatorLength = 1;
  constexpr std::size_t kUncPrefixLength = 2;
  constexpr std::size_t kExtendedPrefixLength =
      sizeof(kExtendedPrefix) / sizeof(kExtendedPrefix[0]) - kTerminatorLength;

  if (path.compare(0, kExtendedPrefixLength, kExtendedPrefix,
                   kExtendedPrefixLength) == 0) {
    return path;
  }
  if (path.compare(0, kUncPrefixLength, kUncPrefix, kUncPrefixLength) == 0) {
    std::wstring extended_path = kExtendedUncPrefix;
    extended_path.append(path, kUncPrefixLength, std::wstring::npos);
    return extended_path;
  }
  return std::wstring(kExtendedPrefix) + path;
}

constexpr std::size_t kPartialWideWriteLength = 2;
constexpr std::size_t kLargeUtf8MessageLength = 4096;

struct WideWriteContext {
  std::wstring output;
  std::size_t maximum_write_length = std::numeric_limits<std::size_t>::max();
  std::size_t calls = 0;
};

bool AppendWideOutput(const wchar_t* input, std::size_t input_length,
                      std::size_t* written, void* context_pointer) {
  auto* context = static_cast<WideWriteContext*>(context_pointer);
  const std::size_t write_length =
      std::min(input_length, context->maximum_write_length);
  context->output.append(input, write_length);
  *written = write_length;
  ++context->calls;
  return true;
}

}  // namespace

TEST(Utf8Output, RetriesPartialWideWrites) {
  const char message[] = {'a', '\0', 'b', 'c'};
  WideWriteContext context;
  context.maximum_write_length = kPartialWideWriteLength;

  EXPECT_TRUE(nglog::internal::WriteUtf8AsWide(message, sizeof(message),
                                               AppendWideOutput, &context));

  const wchar_t expected[] = {L'a', L'\0', L'b', L'c'};
  EXPECT_EQ(context.output,
            std::wstring(expected, sizeof(expected) / sizeof(expected[0])));
  EXPECT_GT(context.calls, 1U);
}

TEST(Utf8Output, RejectsInvalidUtf8WithoutWritingToConsole) {
  constexpr char kInvalidMessage[] = {'a', '\xFF', 'b'};
  WideWriteContext context;

  errno = 0;
  EXPECT_FALSE(nglog::internal::WriteUtf8AsWide(
      kInvalidMessage, sizeof(kInvalidMessage), AppendWideOutput, &context));
  EXPECT_EQ(errno, EINVAL);
  EXPECT_TRUE(context.output.empty());
  EXPECT_EQ(context.calls, 0U);
}

TEST(Utf8Output, RejectsLateInvalidUtf8BeforeWritingAnyOutput) {
  std::string message(kLargeUtf8MessageLength, 'a');
  message.push_back('\xFF');
  WideWriteContext context;

  errno = 0;
  EXPECT_FALSE(nglog::internal::WriteUtf8AsWide(message.data(), message.size(),
                                                AppendWideOutput, &context));
  EXPECT_EQ(errno, EINVAL);
  EXPECT_TRUE(context.output.empty());
  EXPECT_EQ(context.calls, 0U);
}

TEST(Utf8Output, RejectsSuccessfulWritesWithoutProgress) {
  constexpr char kMessage[] = "message";
  constexpr std::size_t kTerminatorLength = 1;
  WideWriteContext context;
  context.maximum_write_length = 0;

  EXPECT_FALSE(nglog::internal::WriteUtf8AsWide(
      kMessage, sizeof(kMessage) - kTerminatorLength, AppendWideOutput,
      &context));
  EXPECT_TRUE(context.output.empty());
  EXPECT_EQ(context.calls, 1U);
}

TEST(Utf8Output, ConvertsLargeMessagesInMultipleChunks) {
  const std::string message(kLargeUtf8MessageLength, 'a');
  WideWriteContext context;

  EXPECT_TRUE(nglog::internal::WriteUtf8AsWide(message.data(), message.size(),
                                               AppendWideOutput, &context));
  EXPECT_EQ(context.output,
            std::wstring(kLargeUtf8MessageLength, static_cast<wchar_t>('a')));
  EXPECT_GT(context.calls, 1U);
}

TEST(Utf8Output, WritesWideTextInLengthDelimitedChunks) {
  constexpr wchar_t kMessage[] = {L'a', L'\0', L'b', L'c'};
  WideWriteContext context;

  EXPECT_TRUE(nglog::internal::WriteWideAsChunks(
      kMessage, sizeof(kMessage) / sizeof(kMessage[0]), AppendWideOutput,
      &context));

  EXPECT_EQ(context.output, std::wstring(L"abc"));
}

TEST(Utf8Output, WritesUnicodeToConsole) {
  HANDLE console = CreateConsoleScreenBuffer(
      GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
      CONSOLE_TEXTMODE_BUFFER, nullptr);
  if (console == INVALID_HANDLE_VALUE) {
    GTEST_SKIP() << "Windows console is unavailable";
  }

  const int file_descriptor = _open_osfhandle(
      reinterpret_cast<std::intptr_t>(console), _O_WRONLY | _O_BINARY);
  ASSERT_NE(file_descriptor, -1);
  std::FILE* stream = _fdopen(file_descriptor, "wb");
  ASSERT_NE(stream, nullptr);

  const std::string message = "caf\xC3\xA9 \xE2\x98\x83";
  ASSERT_TRUE(
      nglog::internal::WriteUtf8(stream, message.data(), message.size()));

  constexpr DWORD kExpectedCharacterCount = 6;
  constexpr std::size_t kTerminatorLength = 1;
  constexpr std::size_t kOutputBufferLength =
      static_cast<std::size_t>(kExpectedCharacterCount) + kTerminatorLength;
  std::array<wchar_t, kOutputBufferLength> output_buffer = {};
  COORD position = {0, 0};
  DWORD characters_read = 0;
  ASSERT_TRUE(ReadConsoleOutputCharacterW(console, output_buffer.data(),
                                          kExpectedCharacterCount, position,
                                          &characters_read));
  ASSERT_EQ(characters_read, kExpectedCharacterCount);
  EXPECT_EQ(std::wstring(L"caf\u00e9 \u2603"),
            std::wstring(output_buffer.data(), characters_read));

  constexpr char kInvalidMessage[] = {'a', '\xFF', 'b'};
  CONSOLE_SCREEN_BUFFER_INFO before_invalid = {};
  ASSERT_TRUE(GetConsoleScreenBufferInfo(console, &before_invalid));
  EXPECT_FALSE(nglog::internal::WriteUtf8(stream, kInvalidMessage,
                                          sizeof(kInvalidMessage)));
  CONSOLE_SCREEN_BUFFER_INFO after_invalid = {};
  ASSERT_TRUE(GetConsoleScreenBufferInfo(console, &after_invalid));
  EXPECT_EQ(after_invalid.dwCursorPosition.X,
            before_invalid.dwCursorPosition.X);
  EXPECT_EQ(after_invalid.dwCursorPosition.Y,
            before_invalid.dwCursorPosition.Y);
  std::fclose(stream);
}
#endif

int main(int argc, char** argv) {
  InitializeLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);

  CHECK_EQ(RUN_ALL_TESTS(), 0);
}
