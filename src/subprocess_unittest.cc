// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//
// Unit tests for Subprocess (subprocess.h/.cc), exercised against the
// subprocess_test_helper companion program rather than an OS-shipped utility,
// so the same test works identically on every platform Subprocess supports.

#include "subprocess.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include "config.h"
#include "ng-log/platform.h"
#include "testing_utilities.h"

#ifdef NGLOG_OS_WINDOWS
#  include <gmock/gmock.h>
#  include <process.h>
#  include <windows.h>

#  include <array>
#  include <cstdint>
#  include <cstdio>
#  include <cstring>
#  include <vector>

#  include "internal/utf8.h"
#  include "long_path_test.h"
#else
#  include <fcntl.h>
#endif

#if !defined(NGLOG_OS_WINDOWS) && \
    (defined(HAVE_FORK) || (defined(HAVE__FORK) && defined(HAVE_EXECV)))
#  include <sys/wait.h>
#  include <unistd.h>
#endif  // POSIX fork() support

#if !defined(NGLOG_OS_WINDOWS) && defined(HAVE_FORK)
#  include <sys/types.h>

#  include <cerrno>

#  include "subprocess_fork.h"
#endif  // POSIX fork() fallback

#if !defined(NGLOG_OS_WINDOWS) && defined(HAVE__FORK) && defined(HAVE_EXECV)
#  include <pthread.h>
#  include <sys/stat.h>
#endif  // POSIX signal-safe subprocess support

using namespace nglog;
using namespace std::chrono_literals;

namespace {

// Generous enough for a loaded machine to stall the helper without failing
// tests that do not exercise timeouts. Reads and waits return as soon as the
// helper responds, so the bound costs nothing otherwise.
constexpr std::chrono::milliseconds kTimeout = 60s;

// Bounds waits on a helper that never exits on its own, which only forced
// termination ends.
constexpr std::chrono::milliseconds kUnresponsiveTimeout = 100ms;

// Large enough to hold the echoed message with room to spare.
constexpr std::size_t kOutputBufferSize = 64;

#if !defined(NGLOG_OS_WINDOWS)
static_assert(noexcept(std::declval<Subprocess<>&>().Spawn(nullptr, nullptr)),
              "subprocess operations must not throw");
#endif  // POSIX normal subprocess does not throw
static_assert(noexcept(std::declval<Subprocess<>&>().WriteStdin(nullptr, 0,
                                                                kTimeout)),
              "subprocess operations must not throw");
static_assert(noexcept(std::declval<Subprocess<>&>().CloseStdin()),
              "subprocess operations must not throw");
static_assert(noexcept(std::declval<Subprocess<>&>().ReadStdout(nullptr, 0,
                                                                kTimeout)),
              "subprocess operations must not throw");
static_assert(noexcept(std::declval<Subprocess<>&>().Wait(kTimeout)),
              "subprocess operations must not throw");
static_assert(std::is_nothrow_destructible<Subprocess<>>::value,
              "subprocess operations must not throw");

char helper_path[] = SUBPROCESS_HELPER_PATH;
char hang_flag[] = "--hang";
char fail_flag[] = "--fail";

template <SubprocessMode mode>
struct SubprocessTestMode {
  static_assert(kSubprocessModeIsSpecialized<mode>,
                "subprocess test mode must be specialized");
  static constexpr SubprocessMode kMode = mode;
  using Process = Subprocess<mode>;
};

// Names each typed test instantiation after the subprocess mode it covers.
struct SubprocessModeName {
  template <typename Mode>
  static std::string GetName(int /*index*/) {
    switch (Mode::kMode) {
      case SubprocessMode::kNormal:
        return "Normal";
      case SubprocessMode::kSignalSafe:
        return "SignalSafe";
    }
    return {};
  }
};

template <bool signal_safe_specialized>
struct SubprocessTestModes;

template <>
struct SubprocessTestModes<true> {
  using Types =
      ::testing::Types<SubprocessTestMode<SubprocessMode::kNormal>,
                       SubprocessTestMode<SubprocessMode::kSignalSafe>>;
};

template <>
struct SubprocessTestModes<false> {
  using Types = ::testing::Types<SubprocessTestMode<SubprocessMode::kNormal>>;
};

using SubprocessTestTypes = typename SubprocessTestModes<
    kSubprocessModeIsSpecialized<SubprocessMode::kSignalSafe>>::Types;

template <typename Mode>
class SubprocessTest : public ::testing::Test {
 protected:
  using Process = typename Mode::Process;
};

TYPED_TEST_SUITE(SubprocessTest, SubprocessTestTypes, SubprocessModeName);

// Reads the standard output of |process| until end of file or until the
// output buffer is full.
template <typename Process>
std::string ReadStdoutToEnd(Process& process) {
  char out[kOutputBufferSize];
  std::size_t total_read = 0;

  while (total_read < sizeof(out)) {
    const std::size_t bytes_read = process.ReadStdout(
        out + total_read, sizeof(out) - total_read, kTimeout);

    if (bytes_read == 0) {
      break;
    }

    total_read += bytes_read;
  }

  return std::string(out, total_read);
}

#if !defined(NGLOG_OS_WINDOWS) && defined(HAVE__FORK) && defined(HAVE_EXECV)
volatile sig_atomic_t atfork_prepare_called = 0;

void MarkAtForkPrepare() { atfork_prepare_called = 1; }

static_assert(noexcept(std::declval<Subprocess<SubprocessMode::kSignalSafe>&>()
                           .Wait(kTimeout)),
              "signal-safe subprocess operations must not throw");
static_assert(noexcept(std::declval<Subprocess<SubprocessMode::kSignalSafe>&>()
                           .Spawn(nullptr, nullptr)),
              "signal-safe subprocess operations must not throw");
static_assert(noexcept(std::declval<Subprocess<SubprocessMode::kSignalSafe>&>()
                           .WriteStdin(nullptr, 0, kTimeout)),
              "signal-safe subprocess operations must not throw");
static_assert(noexcept(std::declval<Subprocess<SubprocessMode::kSignalSafe>&>()
                           .CloseStdin()),
              "signal-safe subprocess operations must not throw");
static_assert(noexcept(std::declval<Subprocess<SubprocessMode::kSignalSafe>&>()
                           .ReadStdout(nullptr, 0, kTimeout)),
              "signal-safe subprocess operations must not throw");
static_assert(std::is_nothrow_destructible<
                  Subprocess<SubprocessMode::kSignalSafe>>::value,
              "signal-safe subprocess operations must not throw");
#endif  // POSIX signal-safe subprocess support

}  // namespace

TYPED_TEST(SubprocessTest, EchoesStdinToStdout) {
  typename TestFixture::Process process;
  char* argv[] = {helper_path, nullptr};
  char* envp[] = {nullptr};

  ASSERT_TRUE(process.Spawn(argv, envp));

  const char message[] = "hello, subprocess";
  const std::size_t written =
      process.WriteStdin(message, sizeof(message) - 1, kTimeout);
  EXPECT_EQ(written, sizeof(message) - 1);

  process.CloseStdin();

  EXPECT_EQ(ReadStdoutToEnd(process), message);

  process.Wait(kTimeout);
}

TYPED_TEST(SubprocessTest, WaitTerminatesAnUnresponsiveProcess) {
  typename TestFixture::Process process;
  char* argv[] = {helper_path, hang_flag, nullptr};
  char* envp[] = {nullptr};

  ASSERT_TRUE(process.Spawn(argv, envp));

  process.CloseStdin();

  // The helper never exits on its own with --hang. This must return
  // promptly, having forcibly terminated it, rather than hanging the
  // test itself.
  const auto result = process.Wait(kUnresponsiveTimeout);
  EXPECT_EQ(nglog::SubprocessWaitResult::kTimedOut, result.status);
}

TYPED_TEST(SubprocessTest, WaitReportsUnsuccessfulExit) {
  typename TestFixture::Process process;
  char* argv[] = {helper_path, fail_flag, nullptr};
  char* envp[] = {nullptr};

  ASSERT_TRUE(process.Spawn(argv, envp));
  process.CloseStdin();

  const auto result = process.Wait(kTimeout);
  EXPECT_EQ(nglog::SubprocessWaitResult::kExited, result.status);
  EXPECT_EQ(1, result.exit_code);
}

TYPED_TEST(SubprocessTest, WaitAcceptsTheMaximumTimeout) {
  typename TestFixture::Process process;
  char* argv[] = {helper_path, fail_flag, nullptr};
  char* envp[] = {nullptr};

  ASSERT_TRUE(process.Spawn(argv, envp));
  process.CloseStdin();

  const auto result = process.Wait(std::chrono::milliseconds::max());
  EXPECT_EQ(nglog::SubprocessWaitResult::kExited, result.status);
}

// The smallest timeout does not wait at all, like any negative one.
TYPED_TEST(SubprocessTest, WaitAcceptsTheMinimumTimeout) {
  typename TestFixture::Process process;
  char* argv[] = {helper_path, hang_flag, nullptr};
  char* envp[] = {nullptr};

  ASSERT_TRUE(process.Spawn(argv, envp));
  process.CloseStdin();

  const auto result = process.Wait(std::chrono::milliseconds::min());
  EXPECT_EQ(nglog::SubprocessWaitResult::kTimedOut, result.status);
}

#ifndef NGLOG_OS_WINDOWS
namespace {

// Lists the descriptors besides the standard streams that every program this
// process executes inherits, in the format of the helper's --list-fds.
std::string InheritableDescriptors() {
  constexpr int kHighestListedDescriptor = 256;
  std::string listed;
  for (int fd = STDERR_FILENO + 1; fd <= kHighestListedDescriptor; ++fd) {
    const int flags = fcntl(fd, F_GETFD);
    if (flags != -1 && (flags & FD_CLOEXEC) == 0) {
      listed += std::to_string(fd) + '\n';
    }
  }
  return listed;
}

}  // namespace

// Descriptors that the test runner passed to this process without
// close-on-exec reach the child as well and are not caused by the spawn.
TYPED_TEST(SubprocessTest, ChildInheritsOnlyTheStandardStreams) {
  typename TestFixture::Process process;
  char option[] = "--list-fds";
  char* argv[] = {helper_path, option, nullptr};
  char* envp[] = {nullptr};

  const std::string expected = InheritableDescriptors();
  ASSERT_TRUE(process.Spawn(argv, envp));
  process.CloseStdin();

  EXPECT_EQ(ReadStdoutToEnd(process), expected);
  process.Wait(kTimeout);
}
#endif

#ifdef NGLOG_OS_WINDOWS
// Another spawn may hold an inheritable pipe end at the same time, which only
// its own child may receive.
TYPED_TEST(SubprocessTest, ChildInheritsNoHandlesOfOtherSpawns) {
  SECURITY_ATTRIBUTES inheritable{};
  inheritable.nLength = sizeof(inheritable);
  inheritable.bInheritHandle = TRUE;
  HANDLE read_end = nullptr;
  HANDLE write_end = nullptr;
  ASSERT_TRUE(CreatePipe(&read_end, &write_end, &inheritable, 0));
  constexpr std::size_t kHandleValueSize = 32;
  char value[kHandleValueSize];
  std::snprintf(value, sizeof(value), "%llx",
                static_cast<unsigned long long>(
                    reinterpret_cast<std::uintptr_t>(write_end)));

  typename TestFixture::Process process;
  char option[] = "--check-handle";
  char* argv[] = {helper_path, option, value, nullptr};
  char* envp[] = {nullptr};
  ASSERT_TRUE(process.Spawn(argv, envp));
  process.CloseStdin();

  EXPECT_EQ(ReadStdoutToEnd(process), "clean");
  process.Wait(kTimeout);
  CloseHandle(read_end);
  CloseHandle(write_end);
}
#endif

TYPED_TEST(SubprocessTest, PassesUtf8Environment) {
  typename TestFixture::Process process;
  char option[] = "--echo-env";
  char name[] = "NGLOG_SUBPROCESS_TEST_VALUE";
  char entry[] = "NGLOG_SUBPROCESS_TEST_VALUE=caf\xC3\xA9";
  char* argv[] = {helper_path, option, name, nullptr};
  char* envp[] = {entry, nullptr};

  ASSERT_TRUE(process.Spawn(argv, envp));
  process.CloseStdin();

  EXPECT_EQ(ReadStdoutToEnd(process), "caf\xC3\xA9");
  process.Wait(kTimeout);
}

TYPED_TEST(SubprocessTest, SpawnFailsForANonExistentProgram) {
  typename TestFixture::Process process;
  char program[] = "nglog-subprocess-unittest-does-not-exist";
  char* argv[] = {program, nullptr};
  char* envp[] = {nullptr};

  EXPECT_FALSE(process.Spawn(argv, envp));
}

#ifdef NGLOG_OS_WINDOWS
namespace {

// Restores the current directory and PATH of this process when a test that
// changes them returns, including through a failed assertion.
class ScopedProcessEnvironment final {
 public:
  ScopedProcessEnvironment()
      : directory_(GetCurrentDirectoryW(0, nullptr)),
        path_(GetEnvironmentVariableW(L"PATH", nullptr, 0)) {
    has_directory_ =
        !directory_.empty() &&
        GetCurrentDirectoryW(static_cast<DWORD>(directory_.size()),
                             directory_.data()) < directory_.size();
    has_path_ = !path_.empty() &&
                GetEnvironmentVariableW(L"PATH", path_.data(),
                                        static_cast<DWORD>(path_.size())) <
                    path_.size();
  }
  ScopedProcessEnvironment(const ScopedProcessEnvironment&) = delete;
  ScopedProcessEnvironment& operator=(const ScopedProcessEnvironment&) = delete;

  ~ScopedProcessEnvironment() {
    SetEnvironmentVariableW(L"PATH", has_path_ ? path_.data() : nullptr);
    if (has_directory_) {
      SetCurrentDirectoryW(directory_.data());
    }
  }

 private:
  std::vector<wchar_t> directory_;
  std::vector<wchar_t> path_;
  bool has_directory_ = false;
  bool has_path_ = false;
};

constexpr char kExecutableExtension[] = ".exe";

// Returns a mutable NUL-terminated copy of |text| for use in an argument
// vector.
std::vector<char> MutableCString(const std::string& text) {
  std::vector<char> result(text.begin(), text.end());
  result.push_back('\0');
  return result;
}

// Spawns a process and reaps it right away. On failure, reports the error of
// the spawn and stores it in |spawn_error| if given.
::testing::AssertionResult SpawnAndReap(char** argv, char** envp,
                                        DWORD* spawn_error = nullptr) {
  Subprocess<> process;
  const bool spawned = process.Spawn(argv, envp);
  const DWORD error = GetLastError();
  if (spawn_error != nullptr) {
    *spawn_error = error;
  }
  if (!spawned) {
    return ::testing::AssertionFailure() << "CreateProcessW error " << error;
  }
  process.CloseStdin();
  process.Wait(kTimeout);
  return ::testing::AssertionSuccess();
}

}  // namespace

TEST(Subprocess, SpawnsFromUtf8ExecutablePath) {
  std::string temporary_path;
  ASSERT_TRUE(nglog::internal::GetTempPathUtf8(&temporary_path));
  temporary_path += nglog::testing_internal::UniqueTemporaryName(
      "nglog_subprocess_\xE2\x82\xAC_");

  std::wstring wide_directory;
  ASSERT_TRUE(nglog::internal::Utf8ToWide(
      temporary_path.data(), temporary_path.size(), &wide_directory));
  ASSERT_TRUE(CreateDirectoryW(wide_directory.c_str(), nullptr));

  std::wstring source_path;
  ASSERT_TRUE(nglog::internal::Utf8ToWide(SUBPROCESS_HELPER_PATH,
                                          std::strlen(SUBPROCESS_HELPER_PATH),
                                          &source_path));
  const std::wstring copied_path = wide_directory + L"\\helper.exe";
  ASSERT_TRUE(CopyFileW(source_path.c_str(), copied_path.c_str(), FALSE));

  std::string executable_path;
  ASSERT_TRUE(nglog::internal::WideToUtf8(
      copied_path.data(), copied_path.size(), &executable_path));
  std::vector<char> executable = MutableCString(executable_path);
  char* argv[] = {executable.data(), nullptr};
  char* envp[] = {nullptr};
  EXPECT_TRUE(SpawnAndReap(argv, envp));
  EXPECT_TRUE(DeleteFileW(copied_path.c_str()));
  EXPECT_TRUE(RemoveDirectoryW(wide_directory.c_str()));
}

TEST(Subprocess, PassesUtf8Arguments) {
  char program[] = SUBPROCESS_HELPER_PATH;
  char option[] = "--echo-arg";
  char argument[] = "argument_\xE2\x82\xAC";
  char* argv[] = {program, option, argument, nullptr};
  char* envp[] = {nullptr};

  Subprocess<> process;
  ASSERT_TRUE(process.Spawn(argv, envp));
  process.CloseStdin();

  EXPECT_EQ(ReadStdoutToEnd(process), argument);
  process.Wait(kTimeout);
}

TEST(Subprocess, AppendsExecutableExtensionToPath) {
  std::string program_path = SUBPROCESS_HELPER_PATH;
  ASSERT_THAT(program_path, ::testing::EndsWith(kExecutableExtension));
  program_path.resize(program_path.size() - std::strlen(kExecutableExtension));

  std::vector<char> program = MutableCString(program_path);
  char* argv[] = {program.data(), nullptr};
  char* envp[] = {nullptr};
  EXPECT_TRUE(SpawnAndReap(argv, envp));
}

TEST(Subprocess, ResolvesBareNameFromApplicationDirectory) {
  const std::string helper_path = SUBPROCESS_HELPER_PATH;
  const std::size_t separator = helper_path.find_last_of("\\/");
  ASSERT_NE(separator, std::string::npos);
  std::string helper_name = helper_path.substr(separator + 1);
  if (::testing::Value(helper_name,
                       ::testing::EndsWith(kExecutableExtension))) {
    helper_name.resize(helper_name.size() - std::strlen(kExecutableExtension));
  }

  // Neither the search path nor the current directory contains the helper,
  // which resides next to this test executable.
  std::array<wchar_t, MAX_PATH> temporary_directory = {};
  ASSERT_NE(GetTempPathW(static_cast<DWORD>(temporary_directory.size()),
                         temporary_directory.data()),
            0U);
  const ScopedProcessEnvironment saved_environment;
  ASSERT_TRUE(SetCurrentDirectoryW(temporary_directory.data()));
  ASSERT_TRUE(SetEnvironmentVariableW(L"PATH", nullptr));

  std::vector<char> program = MutableCString(helper_name);
  char* argv[] = {program.data(), nullptr};
  char* envp[] = {nullptr};
  EXPECT_TRUE(SpawnAndReap(argv, envp));
}

// A program in the current directory, such as a download folder, must not
// replace a program looked up by name.
TEST(Subprocess, DoesNotResolveBareNameFromCurrentDirectory) {
  std::string directory_path;
  ASSERT_TRUE(nglog::internal::GetTempPathUtf8(&directory_path));
  directory_path +=
      nglog::testing_internal::UniqueTemporaryName("nglog_subprocess_cwd_");
  std::wstring directory;
  ASSERT_TRUE(nglog::internal::Utf8ToWide(directory_path.data(),
                                          directory_path.size(), &directory));
  ASSERT_TRUE(CreateDirectoryW(directory.c_str(), nullptr));

  std::wstring source_path;
  ASSERT_TRUE(nglog::internal::Utf8ToWide(SUBPROCESS_HELPER_PATH,
                                          std::strlen(SUBPROCESS_HELPER_PATH),
                                          &source_path));
  constexpr char kPlantedName[] = "nglog_planted_helper";
  const std::wstring planted_path = directory + L"\\nglog_planted_helper.exe";
  ASSERT_TRUE(CopyFileW(source_path.c_str(), planted_path.c_str(), FALSE));

  bool spawned = false;
  {
    const ScopedProcessEnvironment saved_environment;
    ASSERT_TRUE(SetCurrentDirectoryW(directory.c_str()));
    ASSERT_TRUE(SetEnvironmentVariableW(L"PATH", nullptr));
    std::vector<char> program = MutableCString(kPlantedName);
    char* argv[] = {program.data(), nullptr};
    char* envp[] = {nullptr};
    spawned = SpawnAndReap(argv, envp);
  }
  EXPECT_TRUE(DeleteFileW(planted_path.c_str()));
  EXPECT_TRUE(RemoveDirectoryW(directory.c_str()));
  EXPECT_FALSE(spawned);
}

TEST(Subprocess, ResolvesExecutableBeyondMaxPath) {
  nglog::testing_internal::LongPathTestDirectory directory;
  const std::string directory_name =
      nglog::testing_internal::UniqueTemporaryName(
          "nglog_subprocess_long_path_");
  ASSERT_TRUE(directory.CreateInTemporaryDirectory(directory_name));

  std::wstring helper_path;
  ASSERT_TRUE(nglog::internal::Utf8ToWide(SUBPROCESS_HELPER_PATH,
                                          std::strlen(SUBPROCESS_HELPER_PATH),
                                          &helper_path));
  constexpr wchar_t kHelperNameWide[] = L"long_path_helper.exe";
  const std::wstring copied_helper =
      directory.ExtendedChildPath(kHelperNameWide);
  ASSERT_TRUE(CopyFileW(helper_path.c_str(), copied_helper.c_str(), FALSE));

  const ScopedProcessEnvironment saved_environment;
  ASSERT_TRUE(
      SetEnvironmentVariableW(L"PATH", directory.extended_path().c_str()));

  char helper_name[] = "long_path_helper";
  char empty_path[] = "PATH=";
  char* argv[] = {helper_name, nullptr};
  char* envp[] = {empty_path, nullptr};
  DWORD spawn_error = ERROR_SUCCESS;
  const ::testing::AssertionResult spawned =
      SpawnAndReap(argv, envp, &spawn_error);
  if (!spawned) {
    EXPECT_TRUE(DeleteFileW(copied_helper.c_str()));
    if (spawn_error == ERROR_INVALID_NAME &&
        nglog::testing_internal::IsWine()) {
      GTEST_SKIP() << "Wine does not launch executables through extended paths";
    }
  }
  ASSERT_TRUE(spawned);

  std::string copied_helper_utf8;
  ASSERT_TRUE(nglog::internal::WideToUtf8(
      copied_helper.data(), copied_helper.size(), &copied_helper_utf8));
  std::vector<char> explicit_helper = MutableCString(copied_helper_utf8);
  char* explicit_argv[] = {explicit_helper.data(), nullptr};
  DWORD explicit_spawn_error = ERROR_SUCCESS;
  const ::testing::AssertionResult explicit_spawned =
      SpawnAndReap(explicit_argv, envp, &explicit_spawn_error);
  EXPECT_TRUE(DeleteFileW(copied_helper.c_str()));
  if (!explicit_spawned && explicit_spawn_error == ERROR_INVALID_NAME &&
      nglog::testing_internal::IsWine()) {
    GTEST_SKIP() << "Wine does not launch executables through extended paths";
  }
  ASSERT_TRUE(explicit_spawned);
}
#endif

TYPED_TEST(SubprocessTest, OperatorBoolReflectsSpawnState) {
  typename TestFixture::Process process;
  char* argv[] = {helper_path, nullptr};
  char* envp[] = {nullptr};

  EXPECT_FALSE(process);
  ASSERT_TRUE(process.Spawn(argv, envp));
  EXPECT_TRUE(process);

  process.CloseStdin();
  process.Wait(kTimeout);
}

// Without pipe2(), pipes become close-on-exec only after their creation, so
// the test races on such systems.
#if !defined(NGLOG_OS_WINDOWS) && defined(HAVE_PIPE2)
namespace {

// Highest descriptor that a forked child inspects for inherited pipes.
constexpr int kHighestInspectedDescriptor = 256;
// Exit code of a forked child that found a pipe without close-on-exec.
constexpr int kInheritablePipeExitCode = 3;

// Forks a child that reports whether this process holds a pipe that a
// program it executes would inherit. Uses only async-signal-safe calls in the
// child, since other threads keep running in the parent.
bool HoldsInheritablePipe() {
  const pid_t pid = fork();
  if (pid == 0) {
    for (int fd = STDERR_FILENO + 1; fd <= kHighestInspectedDescriptor; ++fd) {
      const int flags = fcntl(fd, F_GETFD);
      struct stat status;
      if (flags != -1 && (flags & FD_CLOEXEC) == 0 && fstat(fd, &status) == 0 &&
          S_ISFIFO(status.st_mode)) {
        _exit(kInheritablePipeExitCode);
      }
    }
    _exit(EXIT_SUCCESS);
  }
  int status = 0;
  return pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
         WEXITSTATUS(status) == kInheritablePipeExitCode;
}

}  // namespace

// Pipes marked close-on-exec only after their creation leak into programs
// that other threads start in between. The signal-safe mode is limited to
// functions that signal-safety(7) lists and therefore keeps that window.
TEST(Subprocess, PipesAreNeverInheritableByOtherPrograms) {
  constexpr int kSpawns = 500;
  if (HoldsInheritablePipe()) {
    GTEST_SKIP() << "the test process holds an inheritable pipe already";
  }

  std::atomic<bool> done{false};
  std::atomic<int> leaks{0};
  std::thread observer{[&done, &leaks] {
    while (!done.load(std::memory_order_relaxed)) {
      if (HoldsInheritablePipe()) {
        leaks.fetch_add(1, std::memory_order_relaxed);
      }
    }
  }};
  for (int i = 0; i < kSpawns; ++i) {
    Subprocess<> process;
    char* argv[] = {helper_path, fail_flag, nullptr};
    char* envp[] = {nullptr};
    if (process.Spawn(argv, envp)) {
      process.CloseStdin();
      process.Wait(kTimeout);
    }
  }
  done.store(true, std::memory_order_relaxed);
  observer.join();

  EXPECT_EQ(leaks.load(), 0);
}
#endif

#if !defined(NGLOG_OS_WINDOWS) && defined(GTEST_HAS_DEATH_TEST)
// Exits successfully if waiting for a child that has been reaped already
// reports its exit status as unavailable.
template <typename Process>
[[noreturn]] void WaitForChildReapedElsewhere() {
  // With SIGCHLD ignored, the system reaps children itself.
  std::signal(SIGCHLD, SIG_IGN);
  Process process;
  char* argv[] = {helper_path, nullptr};
  char* envp[] = {nullptr};
  if (!process.Spawn(argv, envp)) {
    std::exit(EXIT_FAILURE);
  }
  process.CloseStdin();
  ReadStdoutToEnd(process);
  const auto result = process.Wait(kTimeout);
  std::exit(result.status == nglog::SubprocessWaitResult::kUnavailable
                ? EXIT_SUCCESS
                : EXIT_FAILURE);
}

// Exits successfully if waiting for an unresponsive child reports the timeout
// although the system reaps the killed child itself.
template <typename Process>
[[noreturn]] void WaitForUnresponsiveChildReapedElsewhere() {
  std::signal(SIGCHLD, SIG_IGN);
  Process process;
  char* argv[] = {helper_path, hang_flag, nullptr};
  char* envp[] = {nullptr};
  if (!process.Spawn(argv, envp)) {
    std::exit(EXIT_FAILURE);
  }
  process.CloseStdin();
  const auto result = process.Wait(kUnresponsiveTimeout);
  std::exit(result.status == nglog::SubprocessWaitResult::kTimedOut
                ? EXIT_SUCCESS
                : EXIT_FAILURE);
}

// The exit status of a child reaped elsewhere is unavailable, and its process
// ID may already belong to another process, which must not be killed.
TYPED_TEST(SubprocessTest, WaitReportsAChildReapedElsewhere) {
  EXPECT_EXIT(WaitForChildReapedElsewhere<typename TestFixture::Process>(),
              ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TYPED_TEST(SubprocessTest, WaitReportsTheTimeoutOfAChildReapedElsewhere) {
  EXPECT_EXIT(
      WaitForUnresponsiveChildReapedElsewhere<typename TestFixture::Process>(),
      ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}
#endif

TYPED_TEST(SubprocessTest, OperationsOnAnUnspawnedInstanceAreNoOps) {
  typename TestFixture::Process process;
  char out[kOutputBufferSize];
  EXPECT_EQ(0U, process.ReadStdout(out, sizeof(out), kTimeout));
  EXPECT_EQ(0U, process.WriteStdin("x", 1, kTimeout));
  process.CloseStdin();
  const auto result = process.Wait(kTimeout);
  EXPECT_EQ(nglog::SubprocessWaitResult::kFailed, result.status);
}

TYPED_TEST(SubprocessTest, WriteStdinReturnsZeroOnceAfterCloseStdin) {
  typename TestFixture::Process process;
  char* argv[] = {helper_path, hang_flag, nullptr};
  char* envp[] = {nullptr};

  ASSERT_TRUE(process.Spawn(argv, envp));

  process.CloseStdin();
  EXPECT_EQ(0U, process.WriteStdin("x", 1, kTimeout));

  process.Wait(kUnresponsiveTimeout);
}

TYPED_TEST(SubprocessTest, NegativeTimeoutDoesNotWaitIndefinitely) {
  typename TestFixture::Process process;
  char* argv[] = {helper_path, hang_flag, nullptr};
  char* envp[] = {nullptr};

  ASSERT_TRUE(process.Spawn(argv, envp));
  process.CloseStdin();

  char out[kOutputBufferSize];
  EXPECT_EQ(0U, process.ReadStdout(out, sizeof(out), -1ms));

  process.Wait(kUnresponsiveTimeout);
}

TYPED_TEST(SubprocessTest, MoveConstructionTransfersOwnership) {
  using Process = typename TestFixture::Process;

  char* argv[] = {helper_path, nullptr};
  char* envp[] = {nullptr};

  Process original;
  ASSERT_TRUE(original.Spawn(argv, envp));

  Process moved{std::move(original)};
  EXPECT_FALSE(original);
  EXPECT_TRUE(moved);

  const char message[] = "moved";
  EXPECT_EQ(moved.WriteStdin(message, sizeof(message) - 1, kTimeout),
            sizeof(message) - 1);
  moved.CloseStdin();

  EXPECT_EQ(ReadStdoutToEnd(moved), message);

  moved.Wait(kTimeout);
}

TYPED_TEST(SubprocessTest, MoveAssignmentReplacesAndReapsThePreviousProcess) {
  using Process = typename TestFixture::Process;

  char* argv[] = {helper_path, hang_flag, nullptr};
  char* envp[] = {nullptr};

  Process first;
  ASSERT_TRUE(first.Spawn(argv, envp));

  Process second;
  ASSERT_TRUE(second.Spawn(argv, envp));

  // Overwriting |second| while it still owns a running (hung) process
  // must terminate that process rather than leaking it, and take over
  // |first|'s process.
  second = std::move(first);
  EXPECT_FALSE(first);
  EXPECT_TRUE(second);

  second.CloseStdin();
  second.Wait(kUnresponsiveTimeout);
}

TYPED_TEST(SubprocessTest, DestructorReapsAStillRunningProcess) {
  typename TestFixture::Process process;
  char* argv[] = {helper_path, hang_flag, nullptr};
  char* envp[] = {nullptr};

  // Never explicitly Wait()ed on. The destructor must terminate and
  // reap the child rather than leaking it or hanging.
  ASSERT_TRUE(process.Spawn(argv, envp));
  process.CloseStdin();
}

TYPED_TEST(SubprocessTest, WriteStdinTimesOutOnceThePipeFillsUp) {
  typename TestFixture::Process process;
  char* argv[] = {helper_path, hang_flag, nullptr};
  char* envp[] = {nullptr};

  ASSERT_TRUE(process.Spawn(argv, envp));

  // The helper never reads its stdin with --hang, so writes eventually
  // exceed the pipe's buffer capacity. A zero timeout then makes
  // WriteStdin() observe that immediately rather than blocking.
  constexpr std::size_t kChunkSize = 4096;
  constexpr int kMaxAttempts = 64;
  char chunk[kChunkSize] = {};
  bool saw_timeout = false;

  for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    if (process.WriteStdin(chunk, sizeof(chunk),
                           std::chrono::milliseconds{0}) == 0) {
      saw_timeout = true;
      break;
    }
  }

  EXPECT_TRUE(saw_timeout);
  process.Wait(kUnresponsiveTimeout);
}

TYPED_TEST(SubprocessTest, ZeroTimeoutReadsDoNotLoseOutput) {
  typename TestFixture::Process process;
  char* argv[] = {helper_path, nullptr};
  char* envp[] = {nullptr};

  ASSERT_TRUE(process.Spawn(argv, envp));

  // Polling with a zero timeout makes reads time out while the echoed output
  // is still in flight. Output arriving as such a read is abandoned must be
  // returned rather than silently dropped.
  constexpr std::size_t kTotalSize = std::size_t{256} * 1024;
  constexpr std::size_t kChunkSize = 4096;
  // Printable characters only, since the helper's standard streams may
  // translate line endings or treat control characters as end of file.
  constexpr char kPatternFirst = 'a';
  constexpr std::size_t kPatternPeriod = 26;
  constexpr std::chrono::milliseconds kStreamTimeout = 30s;

  std::string expected(kTotalSize, '\0');
  for (std::size_t i = 0; i < expected.size(); ++i) {
    expected[i] = static_cast<char>(kPatternFirst + i % kPatternPeriod);
  }

  std::string received;
  std::size_t sent = 0;
  char out[kChunkSize];
  const auto deadline = std::chrono::steady_clock::now() + kStreamTimeout;

  while (received.size() < expected.size() &&
         std::chrono::steady_clock::now() < deadline) {
    // Write the next chunk only once everything sent so far has been echoed
    // back, so that neither pipe fills up.
    if (sent < expected.size() && received.size() == sent) {
      sent += process.WriteStdin(expected.data() + sent,
                                 std::min(kChunkSize, expected.size() - sent),
                                 kTimeout);
    }
    received.append(out, process.ReadStdout(out, sizeof(out),
                                            std::chrono::milliseconds{0}));
  }

  process.CloseStdin();
  process.Wait(kTimeout);

  EXPECT_EQ(kTotalSize, sent);
  ASSERT_EQ(sent, received.size());
  // Compares without printing both strings, which would flood the log.
  EXPECT_TRUE(received == expected);
}

#if !defined(NGLOG_OS_WINDOWS) && defined(HAVE__FORK) && defined(HAVE_EXECV)
TEST(Subprocess, SignalSafeModeDoesNotRunAtForkHandlers) {
  static_assert(kSubprocessModeIsSpecialized<SubprocessMode::kSignalSafe>,
                "signal-safe subprocess must be specialized");
  ASSERT_EQ(0, pthread_atfork(&MarkAtForkPrepare, nullptr, nullptr));
  atfork_prepare_called = 0;

  char* argv[] = {helper_path, nullptr};
  char* envp[] = {nullptr};

  Subprocess<SubprocessMode::kSignalSafe> process;
  ASSERT_TRUE(process.Spawn(argv, envp));
  EXPECT_EQ(0, atfork_prepare_called);

  process.CloseStdin();
  process.Wait(kTimeout);
}

TEST(Subprocess, SignalSafeModeHandlesClosedStandardInput) {
  const int saved_stdin = dup(STDIN_FILENO);
  ASSERT_GE(saved_stdin, 0);
  ASSERT_EQ(0, close(STDIN_FILENO));

  char* argv[] = {helper_path, nullptr};
  char* envp[] = {nullptr};
  Subprocess<SubprocessMode::kSignalSafe> process;
  const bool spawned = process.Spawn(argv, envp);

  ASSERT_EQ(0, dup2(saved_stdin, STDIN_FILENO));
  ASSERT_EQ(0, close(saved_stdin));
  ASSERT_TRUE(spawned);

  const char message[] = "closed standard input";
  EXPECT_EQ(sizeof(message) - 1,
            process.WriteStdin(message, sizeof(message) - 1, kTimeout));
  process.CloseStdin();

  EXPECT_EQ(ReadStdoutToEnd(process), message);
  EXPECT_EQ(SubprocessWaitResult::kExited, process.Wait(kTimeout).status);
}
#endif  // POSIX signal-safe subprocess support

#if !defined(NGLOG_OS_WINDOWS) && defined(HAVE_FORK)
namespace {

// Runs the helper through the fork() fallback of normal subprocesses, which
// systems without posix_spawn() use, and returns what it writes to stdout.
std::string RunWithForkFallback(char* const argv[], char* const envp[],
                                internal::ForkFunction fork_process = &fork) {
  int stdin_fds[2];
  int stdout_fds[2];
  int exec_error_fds[2];
  // Only a failed exec writes to the error pipe, which exec closes otherwise.
  if (pipe(stdin_fds) != 0 || pipe(stdout_fds) != 0 ||
      pipe(exec_error_fds) != 0 ||
      fcntl(exec_error_fds[1], F_SETFD, FD_CLOEXEC) != 0) {
    ADD_FAILURE() << "pipe() failed with errno " << errno;
    return {};
  }

  const pid_t pid = internal::SpawnProcessWithFork(
      argv, envp, std::getenv("PATH"), stdin_fds[0], stdout_fds[1],
      exec_error_fds[1], fork_process, &internal::ExecuteWithExecvp);
  close(stdin_fds[0]);
  close(stdin_fds[1]);
  close(stdout_fds[1]);
  close(exec_error_fds[1]);

  std::string output;
  char buffer[kOutputBufferSize];
  ssize_t count;
  while ((count = read(stdout_fds[0], buffer, sizeof(buffer))) > 0) {
    output.append(buffer, static_cast<std::size_t>(count));
  }
  close(stdout_fds[0]);

  int exec_error = 0;
  EXPECT_EQ(0, read(exec_error_fds[0], &exec_error, sizeof(exec_error)))
      << "exec failed with errno " << exec_error;
  close(exec_error_fds[0]);

  if (pid > 0) {
    int status = 0;
    EXPECT_EQ(pid, waitpid(pid, &status, 0));
  } else {
    ADD_FAILURE() << "fork() failed with errno " << errno;
  }
  return output;
}

}  // namespace

TEST(Subprocess, ForkFallbackPassesTheEnvironment) {
  char option[] = "--echo-env";
  char name[] = "NGLOG_SUBPROCESS_TEST_VALUE";
  char entry[] = "NGLOG_SUBPROCESS_TEST_VALUE=fork";
  char* argv[] = {helper_path, option, name, nullptr};
  char* envp[] = {entry, nullptr};

  EXPECT_EQ(RunWithForkFallback(argv, envp), "fork");
}

// Without a standard error stream, the null device opens as descriptor 2 in
// the child and must stay open as its stderr.
TEST(Subprocess, ForkFallbackGivesTheChildStderrWhenItHasNone) {
  char option[] = "--check-stderr";
  char* argv[] = {helper_path, option, nullptr};
  char* envp[] = {nullptr};
  const auto fork_without_stderr = []() -> pid_t {
    const pid_t pid = fork();
    if (pid == 0) {
      close(STDERR_FILENO);
    }
    return pid;
  };

  EXPECT_EQ(RunWithForkFallback(argv, envp, fork_without_stderr), "open");
}

TEST(Subprocess, ForkFallbackSearchesThePathOfThisProcess) {
  const std::string path = helper_path;
  const std::size_t separator = path.rfind('/');
  ASSERT_NE(std::string::npos, separator);
  const nglog::ScopedEnvironmentVariable search_path{
      "PATH", path.substr(0, separator).c_str()};

  std::string program = path.substr(separator + 1);
  char option[] = "--echo-env";
  char name[] = "NGLOG_SUBPROCESS_TEST_VALUE";
  char entry[] = "NGLOG_SUBPROCESS_TEST_VALUE=path";
  char* argv[] = {&program[0], option, name, nullptr};
  char* envp[] = {entry, nullptr};
  EXPECT_EQ(RunWithForkFallback(argv, envp), "path");
}
#endif  // POSIX fork() fallback

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
