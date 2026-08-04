// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
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

#if defined(NGLOG_OS_WINDOWS) && defined(HAVE_GET_THREAD_DESCRIPTION)
#  include <windows.h>
#elif defined(HAVE_PTHREAD_GETNAME_NP)
#  include <pthread.h>
#endif

#include "internal/emscripten_console.h"
#include "internal/utf8.h"
#include "ng-log/logging.h"
#include "testing_utilities.h"

#ifdef NGLOG_OS_WINDOWS
#  include <windows.h>

namespace {

// Defined before any using-directive for nglog so that the macro expansion
// cannot rely on unqualified name lookup.
void LogSystemResultOutsideNamespace(DWORD result) { LOG_SYSRESULT(result); }

}  // namespace
#endif

#ifdef NGLOG_OS_WINDOWS
#  include <fcntl.h>
#  include <io.h>
#  include <process.h>
#  include <sys/stat.h>
#  include <windows.h>
#endif

using namespace nglog;

#ifdef GTEST_HAS_DEATH_TEST
TEST(utilities, InitializeLoggingDeathTest) {
  ASSERT_DEATH(InitializeLogging("foobar"), "");
}
#endif

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

#if defined(NGLOG_OS_WINDOWS) && defined(HAVE_GET_THREAD_DESCRIPTION)
TEST(utilities, SetThreadName) {
  constexpr char kThreadName[] = "TestThread";
  nglog::SetThreadName(kThreadName);

  PWSTR thread_name = nullptr;
  ASSERT_TRUE(
      SUCCEEDED(GetThreadDescription(GetCurrentThread(), &thread_name)));
  ASSERT_NE(thread_name, nullptr);
  EXPECT_STREQ(thread_name, L"TestThread");
  LocalFree(thread_name);
}
#elif defined(HAVE_PTHREAD_GETNAME_NP)
TEST(utilities, SetThreadName) {
  constexpr char kThreadName[] = "TestThread";
  nglog::SetThreadName(kThreadName);

  char thread_name[16];
  ASSERT_EQ(
      pthread_getname_np(pthread_self(), thread_name, sizeof(thread_name)), 0);
  EXPECT_STREQ(thread_name, kThreadName);
}
#endif

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

TEST(LogSysResult, ExpandsOutsideNamespace) {
  LogSystemResultOutsideNamespace(ERROR_SUCCESS);
}

TEST(WindowsUtf8Path, RejectsEmbeddedNull) {
  constexpr std::array<char, 4> kPathWithNull = {'l', 'o', '\0', 'g'};
  errno = 0;
  EXPECT_EQ(nglog::internal::AccessUtf8(kPathWithNull.data(),
                                        kPathWithNull.size(), 0),
            -1);
  EXPECT_EQ(errno, EINVAL);
}

TEST(WindowsUtf8Path, RejectsInvalidUtf8) {
  constexpr char kInvalidUtf8[] = {'\xC3', '\x28'};
  std::wstring wide_path;
  errno = 0;
  EXPECT_FALSE(nglog::internal::Utf8ToWide(kInvalidUtf8, sizeof(kInvalidUtf8),
                                           &wide_path));
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

TEST(Utf8Output, PreservesRedirectedBytes) {
  const std::string message = "caf\xC3\xA9 \xE2\x98\x83";
  const std::unique_ptr<std::FILE> file_owner{std::tmpfile()};
  std::FILE* const file = file_owner.get();
  ASSERT_NE(file, nullptr);

  EXPECT_TRUE(nglog::internal::WriteUtf8(file, message.data(), message.size()));
  EXPECT_EQ(std::ftell(file), static_cast<long>(message.size()));
  ASSERT_EQ(std::fflush(file), 0);
  ASSERT_EQ(std::fseek(file, 0, SEEK_SET), 0);

  std::string output(message.size(), '\0');
  ASSERT_EQ(std::fread(&output[0], 1, output.size(), file), output.size());
  EXPECT_EQ(output, message);
}

TEST(Utf8Output, PreservesLengthDelimitedBytes) {
  const char message[] = {'a', '\0', '\xFF', 'b'};
  const std::unique_ptr<std::FILE> file_owner{std::tmpfile()};
  std::FILE* const file = file_owner.get();
  ASSERT_NE(file, nullptr);

  EXPECT_TRUE(nglog::internal::WriteUtf8(file, message, sizeof(message)));
  ASSERT_EQ(std::fflush(file), 0);
  ASSERT_EQ(std::fseek(file, 0, SEEK_SET), 0);

  char output[sizeof(message)] = {};
  ASSERT_EQ(std::fread(output, 1, sizeof(output), file), sizeof(output));
  EXPECT_EQ(std::memcmp(output, message, sizeof(message)), 0);
}

#ifdef NGLOG_OS_WINDOWS
TEST(Utf8Output, PreservesRedirectedTextModeBytes) {
  const std::unique_ptr<std::FILE> file_owner{std::tmpfile()};
  std::FILE* const file = file_owner.get();
  ASSERT_NE(file, nullptr);
  ASSERT_NE(_setmode(_fileno(file), _O_TEXT), -1);

  constexpr char kMessage[] = {'a', '\n', 'b'};
  ASSERT_TRUE(nglog::internal::WriteUtf8(file, kMessage, sizeof(kMessage)));
  ASSERT_EQ(std::fflush(file), 0);
  ASSERT_EQ(std::fseek(file, 0, SEEK_SET), 0);

  char output[sizeof(kMessage)] = {};
  ASSERT_EQ(std::fread(output, 1, sizeof(output), file), sizeof(output));
  EXPECT_EQ(std::memcmp(output, kMessage, sizeof(kMessage)), 0);
}

TEST(Utf8Output, TranslatesNewlinesOfTextModeFile) {
  const std::unique_ptr<std::FILE> stream_owner{std::tmpfile()};
  std::FILE* const stream = stream_owner.get();
  ASSERT_NE(stream, nullptr);
  ASSERT_NE(_setmode(_fileno(stream), _O_TEXT), -1);

  constexpr char kLoggedMessage[] = "a\n";
  ASSERT_TRUE(nglog::internal::WriteUtf8(stream, kLoggedMessage,
                                         sizeof(kLoggedMessage) - 1));
  ASSERT_GE(std::fputs("b\n", stream), 0);
  ASSERT_EQ(std::fflush(stream), 0);

  const std::intptr_t native_handle = _get_osfhandle(_fileno(stream));
  ASSERT_NE(native_handle, -1);
  const HANDLE handle = reinterpret_cast<HANDLE>(native_handle);
  const LARGE_INTEGER position = {};
  ASSERT_TRUE(SetFilePointerEx(handle, position, nullptr, FILE_BEGIN));

  constexpr char kExpected[] = "a\r\nb\r\n";
  std::array<char, sizeof(kExpected)> output = {};
  DWORD bytes_read = 0;
  ASSERT_TRUE(ReadFile(handle, output.data(), static_cast<DWORD>(output.size()),
                       &bytes_read, nullptr));
  EXPECT_EQ(std::string(output.data(), bytes_read), kExpected);
}

TEST(Utf8Output, TranslatesNewlinesOfTextModeFileDescriptor) {
  constexpr int kFilePermissions = _S_IREAD | _S_IWRITE;
  constexpr int kCreateFlags = _O_CREAT | _O_TRUNC | _O_WRONLY | _O_TEXT;
  constexpr int kReadFlags = _O_RDONLY | _O_BINARY;
  const std::string path =
      "nglog_text_mode_path_" + std::to_string(_getpid()) + ".tmp";

  const int file_descriptor =
      _open(path.c_str(), kCreateFlags, kFilePermissions);
  ASSERT_NE(file_descriptor, -1);
  constexpr char kLoggedMessage[] = "a\n";
  ASSERT_TRUE(nglog::internal::WriteUtf8ToFileDescriptor(
      file_descriptor, kLoggedMessage, sizeof(kLoggedMessage) - 1));
  constexpr char kApplicationMessage[] = "b\n";
  ASSERT_EQ(_write(file_descriptor, kApplicationMessage,
                   sizeof(kApplicationMessage) - 1),
            static_cast<int>(sizeof(kApplicationMessage) - 1));
  ASSERT_EQ(_close(file_descriptor), 0);

  const int read_file_descriptor = _open(path.c_str(), kReadFlags);
  ASSERT_NE(read_file_descriptor, -1);
  constexpr char kExpected[] = "a\r\nb\r\n";
  std::array<char, sizeof(kExpected)> output = {};
  const int bytes_read = _read(read_file_descriptor, output.data(),
                               static_cast<unsigned int>(output.size()));
  ASSERT_GE(bytes_read, 0);
  EXPECT_EQ(std::string(output.data(), static_cast<std::size_t>(bytes_read)),
            kExpected);
  ASSERT_EQ(_close(read_file_descriptor), 0);
  ASSERT_EQ(_unlink(path.c_str()), 0);
}

TEST(Utf8Output, WritesSignalSafeLengthDelimitedBytes) {
  const std::unique_ptr<std::FILE> file_owner{std::tmpfile()};
  std::FILE* const file = file_owner.get();
  ASSERT_NE(file, nullptr);

  constexpr char kMessage[] = {'a', '\0', 'b'};
  ASSERT_TRUE(nglog::internal::WriteSignalSafeToFileDescriptor(
      _fileno(file), kMessage, sizeof(kMessage)));
  ASSERT_EQ(std::fflush(file), 0);
  ASSERT_EQ(std::fseek(file, 0, SEEK_SET), 0);

  char output[sizeof(kMessage)] = {};
  ASSERT_EQ(std::fread(output, 1, sizeof(output), file), sizeof(output));
  EXPECT_EQ(std::memcmp(output, kMessage, sizeof(kMessage)), 0);
}

namespace {

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

TEST(Utf8Output, ReplacesInvalidUtf8) {
  constexpr char kInvalidMessage[] = {'a', '\xFF', 'b'};
  WideWriteContext context;

  EXPECT_TRUE(nglog::internal::WriteUtf8AsWide(
      kInvalidMessage, sizeof(kInvalidMessage), AppendWideOutput, &context));
  EXPECT_EQ(context.output, std::wstring(L"a\uFFFDb"));
}

TEST(Utf8Output, ReplacesLateInvalidUtf8) {
  std::string message(kLargeUtf8MessageLength, 'a');
  message.push_back('\xFF');
  WideWriteContext context;

  EXPECT_TRUE(nglog::internal::WriteUtf8AsWide(message.data(), message.size(),
                                               AppendWideOutput, &context));
  EXPECT_EQ(context.output,
            std::wstring(kLargeUtf8MessageLength, L'a') + L"\uFFFD");
}

TEST(Utf8Output, ReplacesLongRunsOfContinuationBytes) {
  const std::string message(kLargeUtf8MessageLength, '\x80');
  WideWriteContext context;

  EXPECT_TRUE(nglog::internal::WriteUtf8AsWide(message.data(), message.size(),
                                               AppendWideOutput, &context));
  EXPECT_EQ(context.output, std::wstring(kLargeUtf8MessageLength, L'\uFFFD'));
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

namespace {

bool IsHighSurrogate(wchar_t character) {
  constexpr wchar_t kHighSurrogateMin = 0xD800;
  constexpr wchar_t kHighSurrogateMax = 0xDBFF;
  return character >= kHighSurrogateMin && character <= kHighSurrogateMax;
}

bool RecordWideChunk(const wchar_t* input, std::size_t input_length,
                     std::size_t* written, void* context) {
  static_cast<std::vector<std::wstring>*>(context)->emplace_back(input,
                                                                 input_length);
  *written = input_length;
  return true;
}

}  // namespace

TEST(Utf8Output, KeepsSurrogatePairsWithinWideChunks) {
  constexpr std::size_t kChunkLength = 1024;
  constexpr wchar_t kHighSurrogate = 0xD83D;
  constexpr wchar_t kLowSurrogate = 0xDE00;
  std::wstring message(kChunkLength - 1, L'a');
  message += kHighSurrogate;
  message += kLowSurrogate;
  message += L'b';

  std::vector<std::wstring> chunks;
  ASSERT_TRUE(nglog::internal::WriteWideAsChunks(message.data(), message.size(),
                                                 RecordWideChunk, &chunks));

  std::wstring output;
  for (const std::wstring& chunk : chunks) {
    ASSERT_FALSE(chunk.empty());
    EXPECT_FALSE(IsHighSurrogate(chunk.back()))
        << "chunk of length " << chunk.size() << " ends with a high surrogate";
    output += chunk;
  }
  EXPECT_EQ(output, message);
}

TEST(Utf8Output, WritesUnicodeToConsole) {
  HANDLE console = CreateConsoleScreenBuffer(
      GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
      CONSOLE_TEXTMODE_BUFFER, nullptr);
  if (console == INVALID_HANDLE_VALUE) {
    GTEST_SKIP() << "Windows console is unavailable";
  }

  const int file_descriptor = _open_osfhandle(
      reinterpret_cast<intptr_t>(console), _O_WRONLY | _O_BINARY);
  ASSERT_NE(file_descriptor, -1);
  const std::unique_ptr<std::FILE> stream_owner{_fdopen(file_descriptor, "wb")};
  std::FILE* const stream = stream_owner.get();
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
  constexpr std::size_t kInvalidMessageCharacterCount = 3;
  EXPECT_TRUE(nglog::internal::WriteUtf8(stream, kInvalidMessage,
                                         sizeof(kInvalidMessage)));
  std::array<wchar_t, kInvalidMessageCharacterCount> replaced_output = {};
  const COORD replaced_position = {static_cast<SHORT>(kExpectedCharacterCount),
                                   0};
  ASSERT_TRUE(
      ReadConsoleOutputCharacterW(console, replaced_output.data(),
                                  static_cast<DWORD>(replaced_output.size()),
                                  replaced_position, &characters_read));
  ASSERT_EQ(characters_read, replaced_output.size());
  EXPECT_EQ(std::wstring(L"a\uFFFDb"),
            std::wstring(replaced_output.data(), characters_read));
}
#endif

int main(int argc, char** argv) {
  InitializeLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);

  CHECK_EQ(RUN_ALL_TESTS(), 0);
}
