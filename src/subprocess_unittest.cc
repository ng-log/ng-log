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
#include <chrono>
#include <cstddef>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef NGLOG_OS_WINDOWS
#  include <process.h>
#  include <windows.h>

#  include "internal/utf8.h"
#endif

#if !defined(NGLOG_OS_WINDOWS) && defined(HAVE__FORK) && defined(HAVE_EXECV)
#  include <pthread.h>
#  include <signal.h>
#  include <unistd.h>
#endif  // POSIX signal-safe subprocess support

using namespace nglog;
using namespace std::chrono_literals;

namespace {

constexpr std::chrono::milliseconds kTimeout = 5s;

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
  using Process = Subprocess<mode>;
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

TYPED_TEST_SUITE(SubprocessTest, SubprocessTestTypes,
                 ::testing::internal::DefaultNameGenerator);

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

  char out[kOutputBufferSize] = {};
  std::size_t total_read = 0;

  while (total_read < sizeof(out)) {
    const std::size_t bytes_read = process.ReadStdout(
        out + total_read, sizeof(out) - total_read, kTimeout);

    if (bytes_read == 0) {
      break;
    }

    total_read += bytes_read;
  }

  EXPECT_EQ(std::string(out, total_read), message);

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
  const auto result = process.Wait(100ms);
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

TYPED_TEST(SubprocessTest, SpawnFailsForANonExistentProgram) {
  typename TestFixture::Process process;
  char program[] = "nglog-subprocess-unittest-does-not-exist";
  char* argv[] = {program, nullptr};
  char* envp[] = {nullptr};

  EXPECT_FALSE(process.Spawn(argv, envp));
}

#ifdef NGLOG_OS_WINDOWS
TEST(Subprocess, SpawnsFromUtf8ExecutablePath) {
  std::string temporary_path;
  ASSERT_TRUE(nglog::internal::GetTempPathUtf8(&temporary_path));
  temporary_path += "nglog_subprocess_\xE2\x82\xAC_" +
                    std::to_string(_getpid()) + "_" +
                    std::to_string(GetTickCount64());

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
  std::vector<char> executable(executable_path.begin(), executable_path.end());
  executable.push_back('\0');
  char* argv[] = {executable.data(), nullptr};
  char* envp[] = {nullptr};
  Subprocess<> process;
  const bool spawned = process.Spawn(argv, envp);
  const DWORD spawn_error = GetLastError();
  if (spawned) {
    process.CloseStdin();
    process.Wait(kTimeout);
  }
  EXPECT_TRUE(DeleteFileW(copied_path.c_str()));
  EXPECT_TRUE(RemoveDirectoryW(wide_directory.c_str()));
  ASSERT_TRUE(spawned) << "CreateProcessW error " << spawn_error;
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

  char output[kOutputBufferSize] = {};
  const std::size_t bytes_read =
      process.ReadStdout(output, sizeof(output), kTimeout);
  EXPECT_EQ(std::string(output, bytes_read), argument);
  process.Wait(kTimeout);
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

  process.Wait(100ms);
}

TYPED_TEST(SubprocessTest, NegativeTimeoutDoesNotWaitIndefinitely) {
  typename TestFixture::Process process;
  char* argv[] = {helper_path, hang_flag, nullptr};
  char* envp[] = {nullptr};

  ASSERT_TRUE(process.Spawn(argv, envp));
  process.CloseStdin();

  char out[kOutputBufferSize];
  EXPECT_EQ(0U, process.ReadStdout(out, sizeof(out), -1ms));

  process.Wait(100ms);
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

  char out[kOutputBufferSize] = {};
  const std::size_t bytes_read = moved.ReadStdout(out, sizeof(out), kTimeout);
  EXPECT_EQ(std::string(out, bytes_read), message);

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
  second.Wait(100ms);
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
  process.Wait(100ms);
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

  char output[kOutputBufferSize] = {};
  const std::size_t bytes_read =
      process.ReadStdout(output, sizeof(output), kTimeout);
  EXPECT_EQ(std::string(message), std::string(output, bytes_read));
  EXPECT_EQ(SubprocessWaitResult::kExited, process.Wait(kTimeout).status);
}
#endif  // POSIX signal-safe subprocess support

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
