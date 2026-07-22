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
#include <utility>

using namespace nglog;
using namespace std::chrono_literals;

namespace {

constexpr std::chrono::milliseconds kTimeout = 5s;

// Large enough to hold the echoed message with room to spare.
constexpr std::size_t kOutputBufferSize = 64;

char helper_path[] = SUBPROCESS_HELPER_PATH;
char hang_flag[] = "--hang";

}  // namespace

TEST(Subprocess, EchoesStdinToStdout) {
  char* argv[] = {helper_path, nullptr};
  char* envp[] = {nullptr};

  Subprocess process;
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

TEST(Subprocess, WaitTerminatesAnUnresponsiveProcess) {
  char* argv[] = {helper_path, hang_flag, nullptr};
  char* envp[] = {nullptr};

  Subprocess process;
  ASSERT_TRUE(process.Spawn(argv, envp));

  process.CloseStdin();

  // The helper never exits on its own with --hang. This must return
  // promptly, having forcibly terminated it, rather than hanging the
  // test itself.
  process.Wait(100ms);
}

TEST(Subprocess, SpawnFailsForANonExistentProgram) {
  char program[] = "nglog-subprocess-unittest-does-not-exist";
  char* argv[] = {program, nullptr};
  char* envp[] = {nullptr};

  Subprocess process;
  EXPECT_FALSE(process.Spawn(argv, envp));
}

TEST(Subprocess, OperatorBoolReflectsSpawnState) {
  char* argv[] = {helper_path, nullptr};
  char* envp[] = {nullptr};

  Subprocess process;
  EXPECT_FALSE(process);
  ASSERT_TRUE(process.Spawn(argv, envp));
  EXPECT_TRUE(process);

  process.CloseStdin();
  process.Wait(kTimeout);
}

TEST(Subprocess, OperationsOnAnUnspawnedInstanceAreNoOps) {
  Subprocess process;

  char out[kOutputBufferSize];
  EXPECT_EQ(0U, process.ReadStdout(out, sizeof(out), kTimeout));
  EXPECT_EQ(0U, process.WriteStdin("x", 1, kTimeout));
  process.CloseStdin();
  process.Wait(kTimeout);
}

TEST(Subprocess, WriteStdinReturnsZeroOnceAfterCloseStdin) {
  char* argv[] = {helper_path, hang_flag, nullptr};
  char* envp[] = {nullptr};

  Subprocess process;
  ASSERT_TRUE(process.Spawn(argv, envp));

  process.CloseStdin();
  EXPECT_EQ(0U, process.WriteStdin("x", 1, kTimeout));

  process.Wait(100ms);
}

TEST(Subprocess, NegativeTimeoutDoesNotWaitIndefinitely) {
  char* argv[] = {helper_path, hang_flag, nullptr};
  char* envp[] = {nullptr};

  Subprocess process;
  ASSERT_TRUE(process.Spawn(argv, envp));
  process.CloseStdin();

  char out[kOutputBufferSize];
  EXPECT_EQ(0U, process.ReadStdout(out, sizeof(out), -1ms));

  process.Wait(100ms);
}

TEST(Subprocess, MoveConstructionTransfersOwnership) {
  char* argv[] = {helper_path, nullptr};
  char* envp[] = {nullptr};

  Subprocess original;
  ASSERT_TRUE(original.Spawn(argv, envp));

  Subprocess moved{std::move(original)};
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

TEST(Subprocess, MoveAssignmentReplacesAndReapsThePreviousProcess) {
  char* argv[] = {helper_path, hang_flag, nullptr};
  char* envp[] = {nullptr};

  Subprocess first;
  ASSERT_TRUE(first.Spawn(argv, envp));

  Subprocess second;
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

TEST(Subprocess, DestructorReapsAStillRunningProcess) {
  char* argv[] = {helper_path, hang_flag, nullptr};
  char* envp[] = {nullptr};

  // Never explicitly Wait()ed on. The destructor must terminate and
  // reap the child rather than leaking it or hanging.
  Subprocess process;
  ASSERT_TRUE(process.Spawn(argv, envp));
  process.CloseStdin();
}

TEST(Subprocess, WriteStdinTimesOutOnceThePipeFillsUp) {
  char* argv[] = {helper_path, hang_flag, nullptr};
  char* envp[] = {nullptr};

  Subprocess process;
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

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
