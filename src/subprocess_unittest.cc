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
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#ifdef NGLOG_OS_WINDOWS
#  include <process.h>
#  include <windows.h>

#  include "internal/utf8.h"
#endif

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
  Subprocess process;
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

  Subprocess process;
  ASSERT_TRUE(process.Spawn(argv, envp));
  process.CloseStdin();

  char output[kOutputBufferSize] = {};
  const std::size_t bytes_read =
      process.ReadStdout(output, sizeof(output), kTimeout);
  EXPECT_EQ(std::string(output, bytes_read), argument);
  process.Wait(kTimeout);
}
#endif

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
