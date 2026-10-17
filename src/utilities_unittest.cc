// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Shinichiro Hamaji
#include "utilities.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sys/types.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <regex>
#include <string>

#if defined(NGLOG_OS_WINDOWS) && defined(HAVE_GET_THREAD_DESCRIPTION)
#  include <windows.h>
#elif defined(HAVE_PTHREAD_GETNAME_NP)
#  include <pthread.h>
#endif

#include "config.h"
#include "internal/emscripten_console.h"
#include "internal/utf8.h"
#include "ng-log/logging.h"
#include "ng-log/platform.h"
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

#  include <algorithm>
#  include <cerrno>
#  include <cstdint>
#  include <limits>
#  include <stdexcept>
#  include <vector>

#  include "long_path_test.h"

using nglog::testing_internal::MakeExtendedPath;

namespace {

/**
 * Creates, inspects and removes a file through the length-delimited UTF-8
 * path functions.
 *
 * @param list_directory If not empty, the directory whose listing must
 *   contain @p filename while the file exists.
 */
void ExpectFileRoundTripUtf8(const std::vector<char>& path,
                             const std::string& list_directory = {},
                             const std::string& filename = {}) {
  constexpr int kExistenceMode = 0;
  const int file_descriptor = nglog::internal::OpenUtf8(
      path.data(), path.size(), _O_CREAT | _O_TRUNC | _O_WRONLY | _O_BINARY,
      _S_IREAD | _S_IWRITE);
  ASSERT_NE(file_descriptor, -1) << std::strerror(errno);
  ASSERT_EQ(_close(file_descriptor), 0);

  struct stat file_status = {};
  EXPECT_EQ(nglog::internal::StatUtf8(path.data(), path.size(), &file_status),
            0);
  EXPECT_EQ(
      nglog::internal::AccessUtf8(path.data(), path.size(), kExistenceMode), 0);

  if (!list_directory.empty()) {
    std::vector<std::string> entries;
    EXPECT_TRUE(nglog::internal::ListDirectoryUtf8(
        list_directory.data(), list_directory.size(), &entries));
    EXPECT_THAT(entries, testing::Contains(filename));
  }

  EXPECT_EQ(nglog::internal::UnlinkUtf8(path.data(), path.size()), 0);
}

}  // namespace
#endif

#if defined(HAVE_PWD_H) && defined(HAVE_UNISTD_H) && !defined(NGLOG_OS_WINDOWS)
#  include <pwd.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

#ifdef NGLOG_OS_LINUX
#  include <fcntl.h>
#  include <sched.h>
#endif

using namespace nglog;
using testing::AllOf;
using testing::Contains;
using testing::ElementsAreArray;
using testing::IsEmpty;
using testing::Not;
using testing::StartsWith;

namespace {

constexpr char kPrintUserName[] = "--print-user-name";

MATCHER_P(FullyMatches, regex,
          std::string{negation ? "does not fully match" : "fully matches"} +
              " the regular expression") {
  return std::regex_match(arg, regex);
}

}  // namespace

#if defined(HAVE_PWD_H) && defined(HAVE_UNISTD_H) && \
    !defined(NGLOG_OS_WINDOWS) && !defined(NGLOG_OS_EMSCRIPTEN)
namespace {

const char* g_self_path = nullptr;

//! Exit status of a child whose environment does not support the test.
constexpr int kChildUnsupportedExitCode = 77;

using ChildSetup = bool (*)();

bool NoChildSetup() { return true; }

std::string ExpectedUserName() {
  const uid_t uid = geteuid();
  struct passwd* user = getpwuid(uid);
  return user != nullptr ? user->pw_name : "uid" + std::to_string(uid);
}

/**
 * Runs this test binary with the given @c USER variable and captures the user
 * name it reports.
 *
 * @param setup Runs in the child before the binary is executed. The child
 *   exits with @ref kChildUnsupportedExitCode if it returns @c false.
 * @param user Value of the @c USER variable of the child.
 * @param output Receives the reported user name without the line break.
 * @return The exit status of the child, or -1 if it did not exit normally.
 */
int RunPrintUserName(ChildSetup setup, const char* user, std::string* output) {
  constexpr std::size_t kReadBufferSize = 256;

  int pipe_fds[2];
  if (pipe(pipe_fds) != 0) {
    return -1;
  }

  const pid_t child_pid = fork();
  if (child_pid == -1) {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return -1;
  }
  if (child_pid == 0) {
    close(pipe_fds[0]);
    if (dup2(pipe_fds[1], STDOUT_FILENO) != STDOUT_FILENO) {
      _exit(EXIT_FAILURE);
    }
    close(pipe_fds[1]);
    if (!setup()) {
      _exit(kChildUnsupportedExitCode);
    }
    if (setenv("USER", user, 1) != 0) {
      _exit(EXIT_FAILURE);
    }
    execl(g_self_path, g_self_path, kPrintUserName,
          static_cast<char*>(nullptr));
    _exit(EXIT_FAILURE);
  }

  close(pipe_fds[1]);
  output->clear();
  std::array<char, kReadBufferSize> buffer;
  ssize_t bytes_read;
  while ((bytes_read = read(pipe_fds[0], buffer.data(), buffer.size())) > 0) {
    output->append(buffer.data(), static_cast<std::size_t>(bytes_read));
  }
  close(pipe_fds[0]);
  if (!output->empty() && output->back() == '\n') {
    output->pop_back();
  }

  int child_status;
  if (waitpid(child_pid, &child_status, 0) != child_pid ||
      !WIFEXITED(child_status)) {
    return -1;
  }
  return WEXITSTATUS(child_status);
}

}  // namespace

// The tests run without elevated privileges, so the environment applies. A
// program started with elevated privileges cannot be run from a unit test.
TEST(utilities, UnprivilegedProgramTrustsTheEnvironment) {
  EXPECT_TRUE(TrustsEnvironment());
}

TEST(utilities, EmptyUserEnvironmentUsesSystemUserName) {
  std::string output;
  ASSERT_EQ(RunPrintUserName(&NoChildSetup, "", &output), EXIT_SUCCESS);
  EXPECT_EQ(output, ExpectedUserName());
}

// The user name becomes part of log file names, so it must not lead them into
// other directories.
TEST(utilities, UserEnvironmentWithPathComponentsUsesSystemUserName) {
  for (const char* user : {"a/../../elsewhere", "..", "."}) {
    std::string output;
    ASSERT_EQ(RunPrintUserName(&NoChildSetup, user, &output), EXIT_SUCCESS);
    EXPECT_EQ(output, ExpectedUserName()) << "USER=" << user;
  }
}

#  ifdef NGLOG_OS_LINUX
namespace {

//! User ID inside the namespace. It must not have a passwd entry.
constexpr uid_t kUnknownUserId = 54321;

bool WriteProcFile(const char* path, const std::string& contents) {
  const int fd = open(path, O_WRONLY | O_CLOEXEC);
  if (fd == -1) {
    return false;
  }
  const ssize_t written = write(fd, contents.data(), contents.size());
  close(fd);
  return written == static_cast<ssize_t>(contents.size());
}

bool EnterUserNamespaceAsUnknownUser() {
  const uid_t outer_uid = geteuid();
  if (unshare(CLONE_NEWUSER) != 0) {
    return false;
  }
  return WriteProcFile("/proc/self/uid_map",
                       std::to_string(kUnknownUserId) + " " +
                           std::to_string(outer_uid) + " 1\n");
}

}  // namespace

TEST(utilities, UserWithoutPasswdEntryUsesUserId) {
  if (getpwuid(kUnknownUserId) != nullptr) {
    GTEST_SKIP() << "User ID " << kUnknownUserId << " has a passwd entry";
  }

  std::string output;
  const int status =
      RunPrintUserName(&EnterUserNamespaceAsUnknownUser, "", &output);
  if (status == kChildUnsupportedExitCode) {
    GTEST_SKIP() << "Unprivileged user namespaces are unavailable";
  }
  ASSERT_EQ(status, EXIT_SUCCESS);
  EXPECT_EQ(output, "uid" + std::to_string(kUnknownUserId));
}
#  endif  // NGLOG_OS_LINUX
#endif

#ifdef NGLOG_OS_WINDOWS
TEST(utilities, NonAsciiUserEnvironmentIsUtf8) {
  constexpr wchar_t kUserVariable[] = L"USERNAME";
  constexpr wchar_t kNonAsciiUserName[] = L"J\u00fcrgen";
  constexpr char kExpectedUserName[] = "J\xc3\xbcrgen";
  constexpr std::size_t kReadBufferSize = 256;

  std::wstring self_path;
  ASSERT_TRUE(nglog::testing_internal::GetExecutablePath(&self_path));

  const wchar_t* original_user = _wgetenv(kUserVariable);
  const std::wstring saved_user =
      original_user != nullptr ? original_user : L"";
  ASSERT_EQ(_wputenv_s(kUserVariable, kNonAsciiUserName), 0);

  // cmd.exe strips the outer quotes and keeps the quoted executable path.
  const std::wstring command =
      L"\"\"" + self_path + L"\" " +
      std::wstring(kPrintUserName,
                   kPrintUserName + sizeof(kPrintUserName) - 1) +
      L"\"";
  FILE* child = _wpopen(command.c_str(), L"rb");
  ASSERT_EQ(_wputenv_s(kUserVariable, saved_user.c_str()), 0);
  ASSERT_NE(child, nullptr);

  std::string output;
  std::array<char, kReadBufferSize> buffer;
  std::size_t bytes_read;
  while ((bytes_read = std::fread(buffer.data(), 1, buffer.size(), child)) >
         0) {
    output.append(buffer.data(), bytes_read);
  }
  ASSERT_EQ(_pclose(child), EXIT_SUCCESS);
  while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
    output.pop_back();
  }

  EXPECT_EQ(output, kExpectedUserName);
}
#endif

#ifdef GTEST_HAS_DEATH_TEST
TEST(utilities, InitializeLoggingDeathTest) {
  ASSERT_DEATH(InitializeLogging("foobar"), "");
}
#endif

TEST(TestTmpDir, IsPrivateToTheProcess) {
  EXPECT_NE(TestTmpDir(), SystemTmpDir());
  EXPECT_THAT(TestTmpDir(), StartsWith(SystemTmpDir()));
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
  EXPECT_THAT("app[1].20260817-123456.42.foo+", FullyMatches(timestamp_regex));
  EXPECT_THAT("app11.20260817-123456.42.foo+",
              Not(FullyMatches(timestamp_regex)));

  const std::regex non_timestamp_regex =
      nglog::tools::MakeLogFilenameMatcher("app[1].", ".foo+", false);
  EXPECT_THAT("app[1]..foo+", FullyMatches(non_timestamp_regex));
  EXPECT_THAT("app[1].20260817-123456.42.foo+",
              Not(FullyMatches(non_timestamp_regex)));
}

TEST(utilities, MakeLogFilenameMatcherRequiresBaseFilename) {
  const std::regex regex =
      nglog::tools::MakeLogFilenameMatcher("", ".foo+", true);
  EXPECT_THAT("20260817-123456.42.foo+", Not(FullyMatches(regex)));
}

TEST(EmscriptenConsole, MapsSeverityToConsoleLevel) {
  EXPECT_EQ(internal::EmscriptenLogLevelForSeverity(NGLOG_INFO),
            internal::EmscriptenLogLevel::kOut);
  EXPECT_EQ(internal::EmscriptenLogLevelForSeverity(NGLOG_WARNING),
            internal::EmscriptenLogLevel::kWarn);
  EXPECT_EQ(internal::EmscriptenLogLevelForSeverity(NGLOG_ERROR),
            internal::EmscriptenLogLevel::kError);
  // The debug console only links with assertions enabled and shows messages
  // as warnings in browsers.
  EXPECT_EQ(internal::EmscriptenLogLevelForSeverity(NGLOG_FATAL),
            internal::EmscriptenLogLevel::kError);
}

TEST(utilities, BasenameFollowsTheLastSeparator) {
  EXPECT_STREQ(nglog::tools::const_basename("dir/file.cc"), "file.cc");
  EXPECT_STREQ(nglog::tools::const_basename("file.cc"), "file.cc");
#ifdef NGLOG_OS_WINDOWS
  EXPECT_STREQ(nglog::tools::const_basename("C:\\src/sub\\file.cc"), "file.cc");
  EXPECT_STREQ(nglog::tools::const_basename("./build\\app.exe"), "app.exe");
#endif
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
  EXPECT_THAT(output, ElementsAreArray(kExpected));
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
  ExpectFileRoundTripUtf8(std::vector<char>(path.begin(), path.end()));
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
  EXPECT_THAT(temporary_path, Not(IsEmpty()));

  std::string windows_path;
  ASSERT_TRUE(nglog::internal::GetWindowsDirectoryUtf8(&windows_path));
  EXPECT_THAT(windows_path, Not(IsEmpty()));
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

TEST(WindowsUtf8Path, ClosesDirectoryWhenCallbackThrows) {
  constexpr char kCurrentDirectory[] = ".";
  DWORD handles_before = 0;
  ASSERT_TRUE(GetProcessHandleCount(GetCurrentProcess(), &handles_before));
  if (handles_before == 0) {
    GTEST_SKIP() << "The process handle count is unavailable";
  }

  EXPECT_THROW(nglog::internal::ForEachDirectoryEntryUtf8(
                   kCurrentDirectory, sizeof(kCurrentDirectory) - 1,
                   [](const char*, std::size_t, void*) -> bool {
                     throw std::runtime_error("callback failed");
                   },
                   nullptr),
               std::runtime_error);

  DWORD handles_after = 0;
  ASSERT_TRUE(GetProcessHandleCount(GetCurrentProcess(), &handles_after));
  EXPECT_EQ(handles_after, handles_before);
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
  const nglog::FilePtr file_owner{std::tmpfile()};
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
  const nglog::FilePtr file_owner{std::tmpfile()};
  std::FILE* const file = file_owner.get();
  ASSERT_NE(file, nullptr);

  EXPECT_TRUE(nglog::internal::WriteUtf8(file, message, sizeof(message)));
  ASSERT_EQ(std::fflush(file), 0);
  ASSERT_EQ(std::fseek(file, 0, SEEK_SET), 0);

  char output[sizeof(message)] = {};
  ASSERT_EQ(std::fread(output, 1, sizeof(output), file), sizeof(output));
  EXPECT_THAT(output, ElementsAreArray(message));
}

#ifdef NGLOG_OS_WINDOWS
TEST(Utf8Output, PreservesRedirectedTextModeBytes) {
  const nglog::FilePtr file_owner{std::tmpfile()};
  std::FILE* const file = file_owner.get();
  ASSERT_NE(file, nullptr);
  ASSERT_NE(_setmode(_fileno(file), _O_TEXT), -1);

  constexpr char kMessage[] = {'a', '\n', 'b'};
  ASSERT_TRUE(nglog::internal::WriteUtf8(file, kMessage, sizeof(kMessage)));
  ASSERT_EQ(std::fflush(file), 0);
  ASSERT_EQ(std::fseek(file, 0, SEEK_SET), 0);

  char output[sizeof(kMessage)] = {};
  ASSERT_EQ(std::fread(output, 1, sizeof(output), file), sizeof(output));
  EXPECT_THAT(output, ElementsAreArray(kMessage));
}

TEST(Utf8Output, TranslatesNewlinesOfTextModeFile) {
  const nglog::FilePtr stream_owner{std::tmpfile()};
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
  const nglog::FilePtr file_owner{std::tmpfile()};
  std::FILE* const file = file_owner.get();
  ASSERT_NE(file, nullptr);

  constexpr char kMessage[] = {'a', '\0', 'b'};
  ASSERT_TRUE(nglog::internal::WriteSignalSafeToFileDescriptor(
      _fileno(file), kMessage, sizeof(kMessage)));
  ASSERT_EQ(std::fflush(file), 0);
  ASSERT_EQ(std::fseek(file, 0, SEEK_SET), 0);

  char output[sizeof(kMessage)] = {};
  ASSERT_EQ(std::fread(output, 1, sizeof(output), file), sizeof(output));
  EXPECT_THAT(output, ElementsAreArray(kMessage));
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
  constexpr char kDriveRoot[] = "C:\\";
  constexpr std::size_t kDriveRootLength = sizeof(kDriveRoot) - 1;
  const std::string largest_narrow_path =
      kDriveRoot +
      std::string(kMaximumPathLength - kTerminatorLength - kDriveRootLength,
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

TEST(WindowsUtf8Path, PreparesExtendedPaths) {
  constexpr wchar_t kExtendedUncPrefix[] = L"\\\\?\\UNC\\";
  constexpr wchar_t kExtendedPrefix[] = L"\\\\?\\";
  constexpr wchar_t kUncPrefix[] = L"\\\\";
  constexpr std::size_t kUncPrefixLength =
      sizeof(kUncPrefix) / sizeof(kUncPrefix[0]) - 1;
  const std::wstring long_drive_path = L"C:\\" + std::wstring(MAX_PATH, L'a');
  const std::wstring long_unc_path =
      L"\\\\server\\share\\" + std::wstring(MAX_PATH, L'a');
  const std::wstring extended_path =
      L"\\\\?\\C:\\" + std::wstring(MAX_PATH, L'a');
  const std::wstring relative_path =
      L"parent/child/../" + std::wstring(MAX_PATH, L'a');

  std::wstring prepared;
  ASSERT_TRUE(nglog::internal::PrepareWindowsPath(
      long_drive_path.data(), long_drive_path.size(), &prepared));
  EXPECT_EQ(prepared, std::wstring(kExtendedPrefix) + long_drive_path);

  ASSERT_TRUE(nglog::internal::PrepareWindowsPath(
      long_unc_path.data(), long_unc_path.size(), &prepared));
  EXPECT_EQ(prepared, std::wstring(kExtendedUncPrefix) +
                          long_unc_path.substr(kUncPrefixLength));

  ASSERT_TRUE(nglog::internal::PrepareWindowsPath(
      extended_path.data(), extended_path.size(), &prepared));
  EXPECT_EQ(prepared, extended_path);

  ASSERT_TRUE(nglog::internal::PrepareWindowsPath(
      relative_path.data(), relative_path.size(), &prepared));
  EXPECT_THAT(prepared, testing::StartsWith(kExtendedPrefix));
  EXPECT_THAT(prepared, Not(testing::HasSubstr(L"/")));
  EXPECT_THAT(prepared, Not(testing::HasSubstr(L"\\..\\")));
}

TEST(WindowsUtf8Path, ExtendsShortRelativePathInLongCurrentDirectory) {
  constexpr std::size_t kCurrentDirectoryLength = 200;
  constexpr std::size_t kRelativePathLength = 100;
  std::array<wchar_t, MAX_PATH> previous_directory = {};
  ASSERT_NE(GetCurrentDirectoryW(static_cast<DWORD>(previous_directory.size()),
                                 previous_directory.data()),
            0U);
  std::array<wchar_t, MAX_PATH> temporary_directory = {};
  const DWORD temporary_length =
      GetTempPathW(static_cast<DWORD>(temporary_directory.size()),
                   temporary_directory.data());
  ASSERT_NE(temporary_length, 0U);
  ASSERT_LT(temporary_length, kCurrentDirectoryLength);

  const std::wstring directory =
      std::wstring(temporary_directory.data(), temporary_length) +
      std::wstring(kCurrentDirectoryLength - temporary_length, L'd');
  ASSERT_TRUE(CreateDirectoryW(directory.c_str(), nullptr));
  ASSERT_TRUE(SetCurrentDirectoryW(directory.c_str()));

  const std::string relative_path(kRelativePathLength, 'r');
  const nglog::internal::WindowsPathKind kind =
      nglog::internal::ClassifyWindowsPath(relative_path.data(),
                                           relative_path.size());
  const std::wstring wide_relative_path(relative_path.begin(),
                                        relative_path.end());
  std::wstring prepared;
  const bool prepared_successfully = nglog::internal::PrepareWindowsPath(
      wide_relative_path.data(), wide_relative_path.size(), &prepared);
  constexpr char kShortRelativePath[] = "r";
  const nglog::internal::WindowsPathKind short_kind =
      nglog::internal::ClassifyWindowsPath(kShortRelativePath,
                                           sizeof(kShortRelativePath) - 1);

  SetCurrentDirectoryW(previous_directory.data());
  EXPECT_TRUE(RemoveDirectoryW(directory.c_str()));

  EXPECT_EQ(kind, nglog::internal::WindowsPathKind::kWide);
  EXPECT_EQ(short_kind, nglog::internal::WindowsPathKind::kNarrow);
  ASSERT_TRUE(prepared_successfully);
  EXPECT_EQ(prepared, MakeExtendedPath(directory) + L'\\' + wide_relative_path);
}

TEST(WindowsUtf8Path, OperatesOnLengthDelimitedAsciiPath) {
  const std::string path =
      "nglog_ascii_path_" + std::to_string(_getpid()) + ".tmp";
  ExpectFileRoundTripUtf8(std::vector<char>(path.begin(), path.end()));
}

TEST(WindowsUtf8Path, OperatesBeyondMaxPath) {
  nglog::testing_internal::LongPathTestDirectory directory;
  const std::string directory_name =
      "nglog_long_path_" + std::to_string(_getpid());
  ASSERT_TRUE(directory.Create(directory_name));

  constexpr char kFilename[] = "log_\xE2\x82\xAC.txt";
  const std::string path = directory.ChildPath(kFilename);
  ExpectFileRoundTripUtf8(std::vector<char>(path.begin(), path.end()),
                          directory.relative_path(), kFilename);
}

TEST(WindowsUtf8Path, OperatesOnExtendedLengthPath) {
  constexpr DWORD kRequiredSizeQuery = 0;
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
  ExpectFileRoundTripUtf8(std::vector<char>(path.begin(), path.end()));
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

namespace {

// Provides a console screen buffer whose contents the tests read back. The
// file descriptor owns the console handle.
class Utf8ConsoleOutput : public testing::Test {
 protected:
  void SetUp() override {
    console_ = CreateConsoleScreenBuffer(
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, CONSOLE_TEXTMODE_BUFFER, nullptr);
    if (console_ == INVALID_HANDLE_VALUE) {
      GTEST_SKIP() << "Windows console is unavailable";
    }

    file_descriptor_ = _open_osfhandle(
        reinterpret_cast<std::intptr_t>(console_), _O_WRONLY | _O_BINARY);
    ASSERT_NE(file_descriptor_, -1);
  }

  // Reads up to the given number of characters starting at the position.
  std::wstring ReadScreen(COORD position, std::size_t length) const {
    std::wstring output(length, L'\0');
    DWORD characters_read = 0;
    EXPECT_TRUE(ReadConsoleOutputCharacterW(console_, &output[0],
                                            static_cast<DWORD>(length),
                                            position, &characters_read));
    output.resize(characters_read);
    return output;
  }

  HANDLE console_ = INVALID_HANDLE_VALUE;
  int file_descriptor_ = -1;
};

}  // namespace

TEST_F(Utf8ConsoleOutput, WritesAsciiToFileDescriptor) {
  constexpr std::array<char, 3> kMessage = {'l', 'o', 'g'};
  constexpr COORD kPosition = {0, 0};
  ASSERT_TRUE(nglog::internal::WriteUtf8ToFileDescriptor(
      file_descriptor_, kMessage.data(), kMessage.size()));

  EXPECT_EQ(ReadScreen(kPosition, kMessage.size()), L"log");
  _close(file_descriptor_);
}

TEST_F(Utf8ConsoleOutput, WritesAsciiToFile) {
  const nglog::FilePtr stream{_fdopen(file_descriptor_, "wb")};
  ASSERT_NE(stream, nullptr);

  constexpr std::array<char, 3> kMessage = {'l', 'o', 'g'};
  constexpr COORD kPosition = {0, 0};
  ASSERT_TRUE(nglog::internal::WriteUtf8(stream.get(), kMessage.data(),
                                         kMessage.size()));

  EXPECT_EQ(ReadScreen(kPosition, kMessage.size()), L"log");
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
  EXPECT_THAT(context.output, IsEmpty());
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

MATCHER(EndsWithHighSurrogate, std::string{negation ? "does not end" : "ends"} +
                                   " with a high surrogate") {
  constexpr wchar_t kHighSurrogateMin = 0xD800;
  constexpr wchar_t kHighSurrogateMax = 0xDBFF;
  return !arg.empty() && arg.back() >= kHighSurrogateMin &&
         arg.back() <= kHighSurrogateMax;
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

  EXPECT_THAT(chunks, testing::Each(
                          AllOf(Not(IsEmpty()), Not(EndsWithHighSurrogate()))));

  std::wstring output;
  for (const std::wstring& chunk : chunks) {
    output += chunk;
  }
  EXPECT_EQ(output, message);
}

TEST(Utf8StandardStream, RedirectionToReusedHandleWritesFile) {
  DWORD mode = 0;
  const std::intptr_t console_handle = _get_osfhandle(_fileno(stderr));
  if (!GetConsoleMode(reinterpret_cast<HANDLE>(console_handle), &mode)) {
    GTEST_SKIP() << "standard error is not a console";
  }
  const int saved_stderr = _dup(_fileno(stderr));
  ASSERT_NE(saved_stderr, -1);
  constexpr char kAscii[] = "x\n";
  ASSERT_TRUE(nglog::internal::WriteUtf8(stderr, kAscii, sizeof(kAscii) - 1));

  // Reopening the stream closes the console handle, whose value the file
  // handle usually reuses.
  const std::string path = TestTmpDir() + "/redirected_stderr";
  ASSERT_NE(std::freopen(path.c_str(), "wb", stderr), nullptr);
  const bool reused = _get_osfhandle(_fileno(stderr)) == console_handle;
  const std::string message = "caf\xC3\xA9";
  const bool written =
      nglog::internal::WriteUtf8(stderr, message.data(), message.size());
  std::fflush(stderr);
  ASSERT_EQ(_dup2(saved_stderr, _fileno(stderr)), 0);
  _close(saved_stderr);

  if (!reused) {
    GTEST_SKIP() << "the file did not reuse the console handle value";
  }
  EXPECT_TRUE(written);
  std::FILE* const input = std::fopen(path.c_str(), "rb");
  ASSERT_NE(input, nullptr);
  std::string contents(message.size() + 1, '\0');
  contents.resize(std::fread(&contents[0], 1, contents.size(), input));
  std::fclose(input);
  EXPECT_EQ(contents, message);
}

TEST_F(Utf8ConsoleOutput, WritesUnicode) {
  const nglog::FilePtr stream{_fdopen(file_descriptor_, "wb")};
  ASSERT_NE(stream, nullptr);

  const std::string message = "caf\xC3\xA9 \xE2\x98\x83";
  const std::wstring expected = L"caf\u00e9 \u2603";
  constexpr COORD kPosition = {0, 0};
  ASSERT_TRUE(
      nglog::internal::WriteUtf8(stream.get(), message.data(), message.size()));
  EXPECT_EQ(ReadScreen(kPosition, expected.size()), expected);

  constexpr char kInvalidMessage[] = {'a', '\xFF', 'b'};
  const std::wstring replaced = L"a\uFFFDb";
  const COORD replaced_position = {static_cast<SHORT>(expected.size()), 0};
  EXPECT_TRUE(nglog::internal::WriteUtf8(stream.get(), kInvalidMessage,
                                         sizeof(kInvalidMessage)));
  EXPECT_EQ(ReadScreen(replaced_position, replaced.size()), replaced);
}
#endif

int main(int argc, char** argv) {
#if defined(HAVE_PWD_H) && defined(HAVE_UNISTD_H) && \
    !defined(NGLOG_OS_WINDOWS) && !defined(NGLOG_OS_EMSCRIPTEN)
  g_self_path = argv[0];
#endif
  InitializeLogging(argv[0]);
  if (argc == 2 && std::strcmp(argv[1], kPrintUserName) == 0) {
    std::puts(MyUserName().c_str());
    return 0;
  }
  testing::InitGoogleTest(&argc, argv);

  CHECK_EQ(RUN_ALL_TESTS(), 0);
}
