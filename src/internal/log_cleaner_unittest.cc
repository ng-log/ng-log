// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause

#include <gtest/gtest.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>

#if defined(HAVE_FORK) && defined(HAVE_PTHREAD_ATFORK) && \
    defined(HAVE_SYS_WAIT_H) && defined(HAVE_UNISTD_H)
#  include <sys/wait.h>
#  include <unistd.h>
#endif

#include "base/commandlineflags.h"
#include "ng-log/logging.h"
#include "testing_utilities.h"

#ifdef NGLOG_USE_GFLAGS
using namespace GFLAGS_NAMESPACE;
#endif

#include "internal/character_classification.h"
#include "internal/log_cleaner.h"

namespace nglog {
namespace internal {

class LogCleanerTestPeer {
 public:
  struct FileIdentity {
    std::uintmax_t device;
    std::uintmax_t inode;
  };

  struct FileInformation {
    std::chrono::system_clock::time_point last_modified;
    FileIdentity identity;
  };

  static bool IsLogFromCurrentProject(const std::string& filepath,
                                      const std::string& base_filename,
                                      const std::string& filename_extension) {
    return LogCleaner::IsLogFromCurrentProject(filepath, base_filename,
                                               filename_extension);
  }

  static bool IsLogLastModifiedOver(
      const std::string& filepath,
      const std::chrono::system_clock::time_point& current_time,
      const std::chrono::minutes& overdue, FileIdentity* identity) {
    LogCleaner::FileIdentity private_identity;
    if (!LogCleaner::IsLogLastModifiedOver(filepath, current_time, overdue,
                                           &private_identity)) {
      return false;
    }
    identity->device = private_identity.device;
    identity->inode = private_identity.inode;
    return true;
  }

  static bool GetFileInformation(const std::string& filepath,
                                 FileInformation* information) {
    LogCleaner::FileInformation private_information;
    if (!LogCleaner::GetFileInformation(filepath, &private_information)) {
      return false;
    }
    information->last_modified = private_information.last_modified;
    information->identity.device = private_information.identity.device;
    information->identity.inode = private_information.identity.inode;
    return true;
  }

  static bool GetFileIdentity(const std::string& filepath,
                              FileIdentity* identity) {
    LogCleaner::FileIdentity private_identity;
    if (!LogCleaner::GetFileIdentity(filepath, &private_identity)) {
      return false;
    }
    identity->device = private_identity.device;
    identity->inode = private_identity.inode;
    return true;
  }

  static bool RemoveLogIfUnchanged(const std::string& filepath,
                                   std::uintmax_t device,
                                   std::uintmax_t inode) {
    return LogCleaner::RemoveLogIfUnchanged(filepath, device, inode);
  }

  static bool Stop(LogCleaner& cleaner) { return cleaner.Stop(); }

  static std::chrono::seconds CleanupInterval(const LogCleaner& cleaner) {
    return cleaner.cleanup_interval_;
  }

  static void SetOverdue(LogCleaner& cleaner,
                         const std::chrono::minutes& overdue) {
    cleaner.overdue_ = overdue;
  }

  static void SetEnabled(LogCleaner& cleaner, bool enabled) {
    cleaner.enabled_ = enabled;
  }

  static bool IsEnabled(const LogCleaner& cleaner) { return cleaner.enabled_; }

  static bool IsInChildAfterFork(const LogCleaner& cleaner) {
    return cleaner.in_child_after_fork_;
  }

  static void SetInChildAfterFork(LogCleaner& cleaner, bool in_child) {
    cleaner.in_child_after_fork_ = in_child;
  }

  static void PrepareFork() { LogCleaner::PrepareFork(); }
  static void ChildAfterFork() { LogCleaner::ChildAfterFork(); }

  static void SetPatternDueAfter(
      LogCleaner& cleaner, const std::string& base_filename,
      const std::chrono::steady_clock::time_point& next_cleanup_time) {
    std::lock_guard<CleanerMutex> lock{cleaner.mutex_};
    LogCleaner::LogFilePattern pattern;
    pattern.base_filename_selected = true;
    pattern.next_cleanup_time = next_cleanup_time;
    cleaner.patterns_[base_filename] = pattern;
  }
};

namespace {
std::string TestPath(const char* suffix) {
  const auto* test_info = testing::UnitTest::GetInstance()->current_test_info();
  return TestTmpDir() + "log_cleaner_" + test_info->test_suite_name() + "_" +
         test_info->name() + "_" + suffix;
}
}  // namespace

TEST(CharacterClassification, IsAscii) {
  EXPECT_TRUE(IsDecimalDigit('0'));
  EXPECT_TRUE(IsDecimalDigit('9'));
  EXPECT_FALSE(IsDecimalDigit('/'));
  EXPECT_FALSE(IsDecimalDigit(':'));
  EXPECT_FALSE(IsDecimalDigit('a'));

  EXPECT_TRUE(IsLower('a'));
  EXPECT_TRUE(IsLower('z'));
  EXPECT_FALSE(IsLower('A'));
  EXPECT_FALSE(IsLower('['));

  EXPECT_TRUE(IsUpper('A'));
  EXPECT_TRUE(IsUpper('Z'));
  EXPECT_FALSE(IsUpper('a'));
  EXPECT_FALSE(IsUpper('@'));

  EXPECT_TRUE(IsAlpha('a'));
  EXPECT_TRUE(IsAlpha('Z'));
  EXPECT_FALSE(IsAlpha('0'));
  EXPECT_FALSE(IsAlpha('['));

  EXPECT_TRUE(IsAlphanumeric('a'));
  EXPECT_TRUE(IsAlphanumeric('Z'));
  EXPECT_TRUE(IsAlphanumeric('0'));
  EXPECT_FALSE(IsAlphanumeric('-'));

  EXPECT_TRUE(IsLowerHexDigit('0'));
  EXPECT_TRUE(IsLowerHexDigit('a'));
  EXPECT_TRUE(IsLowerHexDigit('f'));
  EXPECT_FALSE(IsLowerHexDigit('A'));
  EXPECT_FALSE(IsLowerHexDigit('g'));

  EXPECT_TRUE(IsWhitespace(' '));
  EXPECT_TRUE(IsWhitespace('\n'));
  EXPECT_TRUE(IsWhitespace('\r'));
  EXPECT_FALSE(IsWhitespace('a'));
  EXPECT_FALSE(IsWhitespace('\0'));
}

TEST(LogCleaner, RequiresTimestampAndPidSuffix) {
  EXPECT_FALSE(
      LogCleanerTestPeer::IsLogFromCurrentProject("/tmp/app", "/tmp/app", ""));
  EXPECT_TRUE(LogCleanerTestPeer::IsLogFromCurrentProject(
      "/tmp/app20260809-123456.123", "/tmp/app", ""));
}

TEST(LogCleaner, UsesPositiveCleanupInterval) {
  using namespace std::chrono_literals;
  constexpr int kCleanupIntervalSeconds = 0;

  FlagSaver saver;
  FLAGS_logcleansecs = kCleanupIntervalSeconds;
  LogCleaner cleaner;
  cleaner.Enable(1min);
  EXPECT_EQ(LogCleanerTestPeer::CleanupInterval(cleaner), 1s);
  cleaner.Disable();
}

TEST(LogCleaner, SnapshotsCleanupIntervalWhenEnabled) {
  using namespace std::chrono_literals;
  constexpr int kCleanupIntervalSeconds = 1;
  constexpr int kChangedCleanupIntervalSeconds = 0;

  LogCleaner cleaner;
  FlagSaver saver;
  FLAGS_logcleansecs = kCleanupIntervalSeconds;
  cleaner.Enable(1min);
  EXPECT_EQ(LogCleanerTestPeer::CleanupInterval(cleaner), 1s);
  FLAGS_logcleansecs = kChangedCleanupIntervalSeconds;
  cleaner.Disable();
  EXPECT_EQ(LogCleanerTestPeer::CleanupInterval(cleaner), 1s);
}

TEST(LogCleaner, StopReportsWhetherCleanerWasRunning) {
  using namespace std::chrono_literals;

  LogCleaner cleaner;
  EXPECT_FALSE(LogCleanerTestPeer::Stop(cleaner));

  cleaner.Enable(1min);
#if defined(HAVE_FORK) && !defined(HAVE_PTHREAD_ATFORK)
  EXPECT_FALSE(LogCleanerTestPeer::Stop(cleaner));
#else
  EXPECT_TRUE(LogCleanerTestPeer::Stop(cleaner));
#endif

  EXPECT_FALSE(LogCleanerTestPeer::Stop(cleaner));
}

#if defined(HAVE_FORK) && !defined(HAVE_PTHREAD_ATFORK)
TEST(LogCleaner, DoesNotEnableWithoutForkHandlers) {
  using namespace std::chrono_literals;

  LogCleaner cleaner;
  cleaner.Enable(1min);

  EXPECT_FALSE(LogCleanerTestPeer::IsEnabled(cleaner));
  EXPECT_FALSE(LogCleanerTestPeer::Stop(cleaner));
}
#endif

TEST(LogCleaner, DoesNotCleanWhenNeverEnabled) {
  using namespace std::chrono_literals;
  const std::string base_filename = TestPath("base_");
  const std::string filepath = base_filename + "20260809-123456.123";
  std::remove(filepath.c_str());
  {
    std::ofstream file(filepath);
    file << "log";
  }

  LogCleaner cleaner;
  LogCleanerTestPeer::SetOverdue(cleaner, 0min);
  cleaner.AddLogFilePattern(true, base_filename, "");
  EXPECT_TRUE(
      LogCleanerTestPeer::IsLogFromCurrentProject(filepath, base_filename, ""));
  cleaner.Disable();

  EXPECT_EQ(std::remove(filepath.c_str()), 0);
}

TEST(LogCleaner, DoesNotRemoveReplacedFile) {
  using namespace std::chrono_literals;
  const std::string path = TestPath("identity");
  const std::string replacement_path = path + ".replacement";
  std::remove(path.c_str());
  std::remove(replacement_path.c_str());
  {
    std::ofstream file(path);
    file << "original";
  }

  LogCleanerTestPeer::FileIdentity original_identity;
  ASSERT_TRUE(LogCleanerTestPeer::IsLogLastModifiedOver(
      path, std::chrono::system_clock::now() + 1min, 0min, &original_identity));
  {
    std::ofstream file(replacement_path);
    file << "replacement";
  }
  ASSERT_EQ(std::remove(path.c_str()), 0);
  ASSERT_EQ(std::rename(replacement_path.c_str(), path.c_str()), 0);

  EXPECT_FALSE(LogCleanerTestPeer::RemoveLogIfUnchanged(
      path, original_identity.device, original_identity.inode));
  EXPECT_EQ(std::remove(path.c_str()), 0);
}

TEST(LogCleaner, RemovesMatchingFile) {
  const std::string path = TestPath("remove_matching");
  std::remove(path.c_str());
  {
    std::ofstream file(path);
    file << "overdue";
  }

  LogCleanerTestPeer::FileIdentity identity;
  ASSERT_TRUE(LogCleanerTestPeer::GetFileIdentity(path, &identity));

  EXPECT_TRUE(LogCleanerTestPeer::RemoveLogIfUnchanged(path, identity.device,
                                                       identity.inode));
  EXPECT_FALSE(std::ifstream(path).good());
}

TEST(LogCleaner, ReportsStatFailureWhenCheckingIdentity) {
  const std::string path = TestPath("missing");
  std::remove(path.c_str());

  CaptureTestStderr();
  EXPECT_FALSE(LogCleanerTestPeer::RemoveLogIfUnchanged(path, 0, 0));
  EXPECT_FALSE(GetCapturedTestStderr().empty());
}

TEST(LogCleaner, ForkHandlersCoverEveryLiveInstance) {
  using namespace std::chrono_literals;

  // The handlers below act on every live instance, including g_log_cleaner:
  // keep it disabled so this doesn't reset a real running worker.
  g_log_cleaner.Disable();

  LogCleaner other;
  LogCleanerTestPeer::SetEnabled(other, true);

  // Simulate the fork callbacks directly: they must reset every live
  // instance, not just g_log_cleaner.
  LogCleanerTestPeer::PrepareFork();
  LogCleanerTestPeer::ChildAfterFork();

  EXPECT_FALSE(LogCleanerTestPeer::IsEnabled(other));
  EXPECT_TRUE(LogCleanerTestPeer::IsInChildAfterFork(other));

  other.Disable();
  LogCleanerTestPeer::SetInChildAfterFork(g_log_cleaner, false);
}

TEST(LogCleaner, CapturesFileTimeAndIdentityTogether) {
  using namespace std::chrono_literals;
  const std::string path = TestPath("file_information");
  std::remove(path.c_str());
  {
    std::ofstream file(path);
    file << "contents";
  }

  LogCleanerTestPeer::FileInformation information;
  ASSERT_TRUE(LogCleanerTestPeer::GetFileInformation(path, &information));
  const auto now = std::chrono::system_clock::now();
  EXPECT_LE(information.last_modified, now);
  EXPECT_LT(now - information.last_modified, 1min);
  LogCleanerTestPeer::FileIdentity current_identity;
  ASSERT_TRUE(LogCleanerTestPeer::GetFileIdentity(path, &current_identity));
  EXPECT_EQ(information.identity.device, current_identity.device);
  EXPECT_EQ(information.identity.inode, current_identity.inode);
  EXPECT_EQ(std::remove(path.c_str()), 0);
}

#if defined(HAVE_FORK) && defined(HAVE_PTHREAD_ATFORK) && \
    defined(HAVE_SYS_WAIT_H) && defined(HAVE_UNISTD_H)
bool g_interrupt_next_waitpid = false;

pid_t InterruptFirstWaitPid(pid_t pid, int* status, int options) {
  if (g_interrupt_next_waitpid) {
    g_interrupt_next_waitpid = false;
    errno = EINTR;
    return -1;
  }
  return ::waitpid(pid, status, options);
}

TEST(LogCleaner, DoesNotJoinInheritedThreadAfterFork) {
  using namespace std::chrono_literals;
  constexpr std::chrono::milliseconds kPollInterval{10};
  constexpr std::chrono::milliseconds kTimeout{1000};

  if (!IsLoggingInitialized()) {
    InitializeLogging("log_cleaner_unittest");
  }
  g_log_cleaner.Enable(1min);

  const pid_t child = fork();
  ASSERT_NE(child, -1);
  if (child == 0) {
    g_log_cleaner.Disable();
    ShutdownLogging();
    _exit(EXIT_SUCCESS);
  }

  int status = 0;
  const bool child_exited =
      WaitForChildOrKill(child, kPollInterval, kTimeout, &status);
  g_log_cleaner.Disable();

  ASSERT_TRUE(child_exited);
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), EXIT_SUCCESS);
}

TEST(LogCleaner, DoesNotCleanAfterForkInChild) {
  using namespace std::chrono_literals;
  const std::string base_filename = TestPath("base_");
  const std::string filepath = base_filename + "20260809-123456.123";
  std::remove(filepath.c_str());
  {
    std::ofstream file(filepath);
    file << "parent log";
  }

  if (!IsLoggingInitialized()) {
    InitializeLogging("log_cleaner_unittest");
  }
  g_log_cleaner.Enable(0min);
  LogCleanerTestPeer::SetPatternDueAfter(
      g_log_cleaner, base_filename,
      std::chrono::steady_clock::now() + std::chrono::hours{1});

  const pid_t child = fork();
  ASSERT_NE(child, -1);
  if (child == 0) {
    g_log_cleaner.Disable();
    ShutdownLogging();
    _exit(EXIT_SUCCESS);
  }

  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  EXPECT_EQ(access(filepath.c_str(), F_OK), 0);
  g_log_cleaner.Disable();

  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), EXIT_SUCCESS);
  std::remove(filepath.c_str());
}

TEST(LogCleaner, WaitsThroughInterruptedChildStatusCheck) {
  constexpr useconds_t kChildDelayMicroseconds = 100000;
  g_interrupt_next_waitpid = true;
  const pid_t child = fork();
  ASSERT_NE(child, -1);
  if (child == 0) {
    usleep(kChildDelayMicroseconds);
    _exit(EXIT_SUCCESS);
  }

  int status = 0;
  const bool child_exited = WaitForChildOrKill(
      child, std::chrono::milliseconds{1}, std::chrono::seconds{1}, &status,
      &InterruptFirstWaitPid);

  EXPECT_TRUE(child_exited);
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), EXIT_SUCCESS);
}

// ChildAfterFork() leaves the cleaner disabled, since starting a thread in a
// pthread_atfork() child handler is unsafe. A surviving child is expected to
// call Enable() again to resume cleaning, which this verifies.
TEST(LogCleaner, SurvivingChildResumesCleaningAfterReenable) {
  using namespace std::chrono_literals;
  constexpr std::chrono::milliseconds kPollInterval{10};
  constexpr std::chrono::milliseconds kChildScanTimeout{1000};
  constexpr std::chrono::milliseconds kReapTimeout{2000};
  const std::string base_filename = TestPath("base_");
  const std::string filepath = base_filename + "20260809-123456.123";
  std::remove(filepath.c_str());
  {
    std::ofstream file(filepath);
    file << "parent log";
  }

  if (!IsLoggingInitialized()) {
    InitializeLogging("log_cleaner_unittest");
  }

  const pid_t child = fork();
  ASSERT_NE(child, -1);
  if (child == 0) {
    g_log_cleaner.Enable(0min);
    g_log_cleaner.AddLogFilePattern(true, base_filename, "");
    bool removed = false;
    for (auto elapsed = 0ms; elapsed < kChildScanTimeout;
         elapsed += kPollInterval) {
      if (!std::ifstream(filepath).good()) {
        removed = true;
        break;
      }
      std::this_thread::sleep_for(kPollInterval);
    }
    _exit(removed ? EXIT_SUCCESS : EXIT_FAILURE);
  }

  int status = 0;
  ASSERT_TRUE(WaitForChildOrKill(child, kPollInterval, kReapTimeout, &status));
  g_log_cleaner.Disable();

  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), EXIT_SUCCESS);
  std::remove(filepath.c_str());
}
#endif

}  // namespace internal
}  // namespace nglog

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
