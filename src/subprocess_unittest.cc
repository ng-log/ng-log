// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//
// Unit tests for Subprocess (subprocess.h/.cc), exercised against the
// subprocess_test_helper companion program rather than an OS-shipped utility,
// so the same test works identically on every platform Subprocess supports.

#include "subprocess.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>

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
