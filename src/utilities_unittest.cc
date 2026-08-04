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

#include <array>
#include <cerrno>
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
#endif

int main(int argc, char** argv) {
  InitializeLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);

  CHECK_EQ(RUN_ALL_TESTS(), 0);
}
