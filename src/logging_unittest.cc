// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Ray Sidney

#include <fcntl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "config.h"
#include "ng-log/platform.h"
#ifdef HAVE_GLOB_H
#  include <glob.h>
#endif
#ifndef NGLOG_OS_WINDOWS
#  include <dirent.h>
#endif
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#  include <sys/wait.h>
#endif
#ifdef NGLOG_OS_WINDOWS
#  include <windows.h>

#  include "long_path_test.h"
#  include "windows/port.h"
#endif

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "internal/lock_metrics.h"
#include "internal/path.h"
#include "internal/utf8.h"
#include "mock-log.h"
#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "ng-log/platform.h"
#include "ng-log/raw_logging.h"
#include "stacktrace.h"
#ifdef HAVE_SUBPROCESS
#  include "subprocess.h"
#endif
#include "testing_utilities.h"
#include "utilities.h"

#ifdef NGLOG_OS_WINDOWS
struct WindowsHandleDeleter {
  void operator()(HANDLE handle) const noexcept {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
      CloseHandle(handle);
    }
  }
};

using WindowsHandle =
    std::unique_ptr<std::remove_pointer_t<HANDLE>, WindowsHandleDeleter>;

constexpr std::uint64_t kWindowsDwordBits = std::numeric_limits<DWORD>::digits;
constexpr std::uint64_t kLogFileLockOffset = std::uint64_t{1}
                                             << kWindowsDwordBits;
constexpr DWORD kLogFileLockLength = 1;
#endif

// Introduce several symbols from gmock.
using nglog::nglog_testing::ScopedMockLog;
using testing::_;
using testing::AllOf;
using testing::AnyNumber;
using testing::HasSubstr;
using testing::InitGoogleMock;
using testing::SaveArg;
using testing::StrEq;
using testing::StrictMock;
using testing::StrNe;

using namespace std;
using namespace nglog;

// Some non-advertised functions that we want to test or use.
namespace nglog {
namespace base {
namespace internal {
bool GetExitOnDFatal();
void SetExitOnDFatal(bool value);
}  // namespace internal
}  // namespace base
}  // namespace nglog

static void TestLogging(bool check_counts);
static void TestRawLogging();
static void LogWithLevels(int v, int severity, bool err, bool alsoerr);
static void TestLoggingLevels();
static void TestVLogModule();
static void TestLogString();
static void TestLogSink();
static void TestLogToString();
static void TestLogSinkWaitTillSent();
static void TestCHECK();
static void TestDCHECK();
static void TestSTREQ();

namespace {

// Dynamically generate a prefix using the default format and write it to the
// stream.
void PrefixAttacher(std::ostream& s, const LogMessage& m, void* data) {
  // Assert that `data` contains the expected contents before producing the
  // prefix (otherwise causing the tests to fail):
  if (data == nullptr || *static_cast<string*>(data) != "good data") {
    return;
  }

  s << GetLogSeverityName(m.severity())[0] << setw(4) << 1900 + m.time().year()
    << setw(2) << 1 + m.time().month() << setw(2) << m.time().day() << ' '
    << setw(2) << m.time().hour() << ':' << setw(2) << m.time().min() << ':'
    << setw(2) << m.time().sec() << "." << setw(6) << m.time().usec() << ' '
    << setfill(' ') << setw(5) << m.thread_id() << setfill('0') << ' '
    << m.basename() << ':' << m.line() << "]";
}

[[noreturn]] void ThrowFatalLogFailure() {
  throw std::logic_error{"fatal log"};
}

// Captured pre-init, re-emitted by LoggingGoldenFile.Stderr.
std::string early_stderr;

// argv[0] and the prefix formatter context, kept so
// Logging.CustomLoggerDeletionOnShutdown can restore both after its own
// ShutdownLogging() call.
const char* g_argv0 = nullptr;
std::string g_prefix_attacher_data;

#ifdef HAVE_SUBPROCESS
bool SetMailerMarkerEnvironment(const std::string& marker_path) {
#  ifdef NGLOG_OS_WINDOWS
  return _putenv_s("NGLOG_TEST_MAILER_MARKER", marker_path.c_str()) == 0;
#  else
  return setenv("NGLOG_TEST_MAILER_MARKER", marker_path.c_str(), 1) == 0;
#  endif
}

bool MailerMarkerExists(const std::string& marker_path) {
  std::ifstream marker{marker_path};
  return marker.good();
}

int RunSlowMailerScenario(const char* program, const char* marker_path,
                          const char* log_path) {
  using namespace std::chrono_literals;
  constexpr auto kPollInterval = 10ms;

  if (!SetMailerMarkerEnvironment(marker_path)) {
    return EXIT_FAILURE;
  }
  nglog::SetFlag(FLAGS_logtostderr, false);
  nglog::SetFlag(FLAGS_logtostdout, false);
  nglog::SetFlag(FLAGS_logmailer, EMAIL_TEST_HELPER_PATH);
  InitializeLogging(program);
  SetEmailLogging(NGLOG_WARNING, "example@example.com");
  SetLogDestination(NGLOG_INFO, log_path);

  std::thread mail_logger{[] { LOG(WARNING) << "first email"; }};
  // A mailer that never starts surfaces as a hang reported by the CTest
  // timeout.
  while (!MailerMarkerExists(marker_path)) {
    std::this_thread::sleep_for(kPollInterval);
  }

  std::mutex other_log_mutex;
  std::condition_variable other_log_condition;
  bool other_log_finished = false;
  std::thread other_logger{
      [&other_log_finished, &other_log_mutex, &other_log_condition] {
        LOG(INFO) << "second log";
        {
          std::lock_guard<std::mutex> lock{other_log_mutex};
          other_log_finished = true;
        }
        other_log_condition.notify_one();
      }};
  {
    std::unique_lock<std::mutex> lock{other_log_mutex};
    other_log_condition.wait(
        lock, [&other_log_finished] { return other_log_finished; });
  }

  other_logger.join();
  mail_logger.join();
  std::fputs("passed\n", stdout);
  std::fflush(stdout);
  return EXIT_SUCCESS;
}
#endif  // HAVE_SUBPROCESS

#ifdef HAVE_POSIX_LONG_PATH_SUPPORT
class PosixLongPathTestDirectory final {
 public:
  PosixLongPathTestDirectory() = default;
  PosixLongPathTestDirectory(const PosixLongPathTestDirectory&) = delete;
  PosixLongPathTestDirectory& operator=(const PosixLongPathTestDirectory&) =
      delete;

  ~PosixLongPathTestDirectory() { Cleanup(); }

  void Cleanup() {
    if (directory_fds_.empty()) {
      if (root_created_) {
        rmdir(root_path_.c_str());
        root_created_ = false;
        root_path_.clear();
      }
      return;
    }

    for (const int descriptor : directory_fds_) {
      fchmod(descriptor, S_IRWXU);
    }

    const int contents_fd = dup(directory_fds_.back());
    if (contents_fd != -1) {
      DIR* directory = fdopendir(contents_fd);
      if (directory != nullptr) {
        while (const struct dirent* entry = readdir(directory)) {
          if (std::strcmp(entry->d_name, ".") != 0 &&
              std::strcmp(entry->d_name, "..") != 0) {
            unlinkat(directory_fds_.back(), entry->d_name, 0);
          }
        }
        closedir(directory);
      } else {
        close(contents_fd);
      }
    }

    for (std::size_t index = directory_fds_.size(); index > 1; --index) {
      close(directory_fds_[index - 1]);
      unlinkat(directory_fds_[index - 2], components_[index - 2].c_str(),
               AT_REMOVEDIR);
    }
    close(directory_fds_.front());
    rmdir(root_path_.c_str());
    directory_fds_.clear();
    components_.clear();
    root_created_ = false;
    root_path_.clear();
    path_.clear();
  }

  bool CreateInTemporaryDirectory(const std::string& name) {
    return Create(TestTmpDir() + '/' + name);
  }

  bool CreateInCurrentDirectory(const std::string& name) {
    return Create(name);
  }

  const std::string& path() const { return path_; }

  bool MakeSearchOnly() {
    for (const int descriptor : directory_fds_) {
      if (fchmod(descriptor, S_IXUSR) != 0) {
        return false;
      }
    }
    return true;
  }

  std::string ChildPath(const std::string& name) const {
    return path_ + '/' + name;
  }

  std::string ReadChild(const char* name) const {
    FileDescriptor fd{openat(directory_fds_.back(), name, O_RDONLY)};
    if (!fd) {
      return std::string();
    }

    std::string contents;
    constexpr std::size_t kBufferLength = 4096;
    char buffer[kBufferLength];
    while (true) {
      const ssize_t length = read(fd.get(), buffer, sizeof(buffer));
      if (length <= 0) {
        break;
      }
      contents.append(buffer, static_cast<std::size_t>(length));
    }
    return contents;
  }

 private:
  bool Create(const std::string& root_path) {
    root_path_ = root_path;
    if (mkdir(root_path_.c_str(), S_IRWXU) != 0) {
      return false;
    }
    root_created_ = true;

    const int root_fd = open(root_path_.c_str(), O_RDONLY | O_DIRECTORY);
    if (root_fd == -1) {
      rmdir(root_path_.c_str());
      root_created_ = false;
      root_path_.clear();
      return false;
    }
    directory_fds_.push_back(root_fd);
    path_ = root_path_;

    constexpr long kDefaultPathLimit = 4096;
    constexpr std::size_t kPathLengthMargin = 64;
    const long configured_path_limit =
        pathconf(root_path_.c_str(), _PC_PATH_MAX);
    const std::size_t path_limit = static_cast<std::size_t>(
        configured_path_limit > 0 ? configured_path_limit : kDefaultPathLimit);
    constexpr char kComponent[] = "long_path_component";
    while (path_.size() <= path_limit + kPathLengthMargin) {
      const int parent_fd = directory_fds_.back();
      if (mkdirat(parent_fd, kComponent, S_IRWXU) != 0) {
        Cleanup();
        return false;
      }
      const int child_fd =
          openat(parent_fd, kComponent, O_RDONLY | O_DIRECTORY);
      if (child_fd == -1) {
        unlinkat(parent_fd, kComponent, AT_REMOVEDIR);
        Cleanup();
        return false;
      }
      components_.emplace_back(kComponent);
      directory_fds_.push_back(child_fd);
      path_ += '/';
      path_ += kComponent;
    }
    return true;
  }
  std::string root_path_;
  std::string path_;
  std::vector<std::string> components_;
  std::vector<int> directory_fds_;
  bool root_created_ = false;
};
#endif

}  // namespace

int main(int argc, char** argv) {
  g_argv0 = argv[0];

#ifdef HAVE_SUBPROCESS
  if (argc == 4 && std::strcmp(argv[1], "--nglog-slow-mailer") == 0) {
    return RunSlowMailerScenario(argv[0], argv[2], argv[3]);
  }
#endif

#ifdef NGLOG_OS_WINDOWS
  if (argc == 4 && std::strcmp(argv[1], "--nglog-hold-log-lock") == 0) {
    WindowsHandle file_handle{
        CreateFileA(argv[2], GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (file_handle.get() == INVALID_HANDLE_VALUE) return EXIT_FAILURE;

    OVERLAPPED overlapped = {};
    overlapped.Offset = static_cast<DWORD>(kLogFileLockOffset);
    overlapped.OffsetHigh =
        static_cast<DWORD>(kLogFileLockOffset >> kWindowsDwordBits);
    if (!LockFileEx(file_handle.get(),
                    LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0,
                    kLogFileLockLength, 0, &overlapped)) {
      return EXIT_FAILURE;
    }

    std::ofstream ready_file{argv[3]};
    ready_file << "ready";
    ready_file.close();
    constexpr DWORD kChildLockHoldMilliseconds = 10'000;
    Sleep(kChildLockHoldMilliseconds);
    return EXIT_SUCCESS;
  }

  if ((argc == 3 || argc == 4) &&
      std::strcmp(argv[1], "--nglog-log-child") == 0) {
    nglog::SetFlag(FLAGS_colorlogtostderr, false);
    nglog::SetFlag(FLAGS_logtostderr, false);
    nglog::SetFlag(FLAGS_timestamp_in_logfile_name, false);
    InitializeLogging(argv[0]);
    SetLogDestination(NGLOG_INFO, argv[2]);
    LOG(INFO) << "message to new base, child - should only appear on STDERR "
                 "not on the file";
    FlushLogFiles(NGLOG_INFO);
    if (argc == 4) {
      std::ofstream ready_file{argv[3]};
      ready_file << "ready";
      ready_file.close();
      constexpr DWORD kChildLockHoldMilliseconds = 10'000;
      Sleep(kChildLockHoldMilliseconds);
    }
    LogToStderr();
    return 0;
  }
#endif

  nglog::SetFlag(FLAGS_colorlogtostderr, false);
  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, true);

  // Make sure stderr is not buffered as stderr seems to be buffered
  // on recent windows.
  setbuf(stderr, nullptr);

  // Test some basics before InitializeLogging:
  CaptureTestStderr();
  LogWithLevels(nglog::GetFlag(FLAGS_v), nglog::GetFlag(FLAGS_stderrthreshold),
                nglog::GetFlag(FLAGS_logtostderr),
                nglog::GetFlag(FLAGS_alsologtostderr));
  LogWithLevels(0, 0, false, false);  // simulate "before global c-tors"
  early_stderr = GetCapturedTestStderr();

  EXPECT_FALSE(IsLoggingInitialized());

  // Setting a custom prefix generator (it will use the default format so that
  // the golden outputs can be reused):
  g_prefix_attacher_data = "good data";
  InitializeLogging(argv[0]);
  InstallPrefixFormatter(&PrefixAttacher, &g_prefix_attacher_data);

  EXPECT_TRUE(IsLoggingInitialized());

  nglog::SetFlag(FLAGS_logtostderr, true);

  testing::InitGoogleTest(&argc, argv);
  InitGoogleMock(&argc, argv);

  nglog::ParseCommandLineFlags(&argc, &argv,
                               nglog::FlagParsingMode::kRemoveFlags);

  // so that death tests run before we use threads
  return RUN_ALL_TESTS();
}

void TestLogging(bool check_counts) {
  int64 base_num_infos = LogMessage::num_messages(NGLOG_INFO);
  int64 base_num_warning = LogMessage::num_messages(NGLOG_WARNING);
  int64 base_num_errors = LogMessage::num_messages(NGLOG_ERROR);

  LOG(INFO) << string("foo ") << "bar " << 10 << ' ' << 3.4;
  for (int i = 0; i < 10; ++i) {
    int old_errno = std::exchange(errno, i);
    PLOG_EVERY_N(ERROR, 2) << "Plog every 2, iteration " << COUNTER;
    errno = old_errno;

    LOG_EVERY_N(ERROR, 3) << "Log every 3, iteration " << COUNTER << endl;
    LOG_EVERY_N(ERROR, 4) << "Log every 4, iteration " << COUNTER << endl;

    LOG_IF_EVERY_N(WARNING, true, 5) << "Log if every 5, iteration " << COUNTER;
    LOG_IF_EVERY_N(WARNING, false, 3)
        << "Log if every 3, iteration " << COUNTER;
    LOG_IF_EVERY_N(INFO, true, 1) << "Log if every 1, iteration " << COUNTER;
    LOG_IF_EVERY_N(ERROR, (i < 3), 2)
        << "Log if less than 3 every 2, iteration " << COUNTER;
  }
  LOG_IF(WARNING, true) << "log_if this";
  LOG_IF(WARNING, false) << "don't log_if this";

  char s[] = "array";
  LOG(INFO) << s;
  const char const_s[] = "const array";
  LOG(INFO) << const_s;
  int j = 1000;
  LOG(ERROR) << string("foo") << ' ' << j << ' ' << setw(10) << j << " "
             << setw(1) << hex << j;
  LOG(INFO) << "foo " << std::setw(10) << 1.0;

  {
    nglog::LogMessage outer(__FILE__, __LINE__, NGLOG_ERROR);
    outer.stream() << "outer";

    LOG(ERROR) << "inner";
  }

  LogMessage("foo", LogMessage::kNoLogPrefix, NGLOG_INFO).stream()
      << "no prefix";

  if (check_counts) {
    // INFO/WARNING totals are exact: their EVERY_N periods either always
    // fire (period 1) or evenly divide the 10 iterations (period 5), so
    // they're independent of the per-call-site counters' starting phase.
    CHECK_EQ(base_num_infos + 15, LogMessage::num_messages(NGLOG_INFO));
    CHECK_EQ(base_num_warning + 3, LogMessage::num_messages(NGLOG_WARNING));
    // ERROR is a range, not an exact count: its EVERY_N periods (3, 4, 2)
    // don't evenly divide their iteration counts, so how many fire depends
    // on the counters' phase, which carries over from any earlier call to
    // this function elsewhere in the process.
    const int64 actual_errors = LogMessage::num_messages(NGLOG_ERROR);
    CHECK_GE(actual_errors, base_num_errors + 14);
    CHECK_LE(actual_errors, base_num_errors + 17);
  }
}

static void NoAllocNewHook() { RAW_LOG(FATAL, "%s", "unexpected new"); }

struct NewHook {
  NewHook() { g_new_hook = &NoAllocNewHook; }
  ~NewHook() { g_new_hook = nullptr; }
};

#ifdef GTEST_HAS_DEATH_TEST
namespace {
// Keep the allocation in a separate function so the death test exercises the
// replaceable global operator new on every supported compiler.
NGLOG_ATTRIBUTE_NOINLINE
int* allocInt() { return new int; }
}  // namespace

TEST(DeathNoAllocNewHook, logging) {
  // Avoid unused warnings under MinGW
  //
  // NOTE MSVC produces warning C4551 here if we do not take the address of the
  // function explicitly.
  (void)&allocInt;
  ASSERT_DEATH(
      {
        NewHook new_hook;
        if (allocInt() == nullptr) {
          std::abort();
        }
      },
      "unexpected new");
}
#endif

void TestRawLogging() {
  auto foo = std::make_unique<string>("foo ");
  string huge_str(50000, 'a');

  nglog::ScopedFlags<> save;

  // Check that RAW logging does not use mallocs.
  NewHook new_hook;

  RAW_LOG(INFO, "%s%s%d%c%f", foo->c_str(), "bar ", 10, ' ', 3.4);
  char s[] = "array";
  RAW_LOG(WARNING, "%s", s);
  const char const_s[] = "const array";
  RAW_LOG(INFO, "%s", const_s);
  void* p = reinterpret_cast<void*>(kPtrTestValue);
  RAW_LOG(INFO, "ptr %p", p);
  p = nullptr;
  RAW_LOG(INFO, "ptr %p", p);
  int j = 1000;
  RAW_LOG(ERROR, "%s%d%c%010d%s%1x", foo->c_str(), j, ' ', j, " ", j);
  RAW_VLOG(0, "foo %d", j);

#if defined(NDEBUG)
  RAW_LOG(INFO, "foo %d", j);  // so that have same stderr to compare
#else
  RAW_DLOG(INFO, "foo %d", j);  // test RAW_DLOG in debug mode
#endif

  // test how long messages are chopped:
  RAW_LOG(WARNING, "Huge string: %s", huge_str.c_str());
  RAW_VLOG(0, "Huge string: %s", huge_str.c_str());

  nglog::SetFlag(FLAGS_v, 0);
  RAW_LOG(INFO, "log");
  RAW_VLOG(0, "vlog 0 on");
  RAW_VLOG(1, "vlog 1 off");
  RAW_VLOG(2, "vlog 2 off");
  RAW_VLOG(3, "vlog 3 off");
  nglog::SetFlag(FLAGS_v, 2);
  RAW_LOG(INFO, "log");
  RAW_VLOG(1, "vlog 1 on");
  RAW_VLOG(2, "vlog 2 on");
  RAW_VLOG(3, "vlog 3 off");

#if defined(NDEBUG)
  RAW_DCHECK(1 == 2, " RAW_DCHECK's shouldn't be compiled in normal mode");
#endif

  RAW_CHECK(1 == 1, "should be ok");
  RAW_DCHECK(true, "should be ok");
}

void LogWithLevels(int v, int severity, bool err, bool alsoerr) {
  RAW_LOG(INFO,
          "Test: v=%d stderrthreshold=%d logtostderr=%d alsologtostderr=%d", v,
          severity, err, alsoerr);

  nglog::ScopedFlags<> save;

  nglog::SetFlag(FLAGS_v, v);
  nglog::SetFlag(FLAGS_stderrthreshold, severity);
  nglog::SetFlag(FLAGS_logtostderr, err);
  nglog::SetFlag(FLAGS_alsologtostderr, alsoerr);

  RAW_VLOG(-1, "vlog -1");
  RAW_VLOG(0, "vlog 0");
  RAW_VLOG(1, "vlog 1");
  RAW_LOG(INFO, "log info");
  RAW_LOG(WARNING, "log warning");
  RAW_LOG(ERROR, "log error");

  VLOG(-1) << "vlog -1";
  VLOG(0) << "vlog 0";
  VLOG(1) << "vlog 1";
  LOG(INFO) << "log info";
  LOG(WARNING) << "log warning";
  LOG(ERROR) << "log error";

  VLOG_IF(-1, true) << "vlog_if -1";
  VLOG_IF(-1, false) << "don't vlog_if -1";
  VLOG_IF(0, true) << "vlog_if 0";
  VLOG_IF(0, false) << "don't vlog_if 0";
  VLOG_IF(1, true) << "vlog_if 1";
  VLOG_IF(1, false) << "don't vlog_if 1";
  LOG_IF(INFO, true) << "log_if info";
  LOG_IF(INFO, false) << "don't log_if info";
  LOG_IF(WARNING, true) << "log_if warning";
  LOG_IF(WARNING, false) << "don't log_if warning";
  LOG_IF(ERROR, true) << "log_if error";
  LOG_IF(ERROR, false) << "don't log_if error";

  int c;
  c = 1;
  VLOG_IF(100, c -= 2) << "vlog_if 100 expr";
  EXPECT_EQ(c, -1);
  c = 1;
  VLOG_IF(0, c -= 2) << "vlog_if 0 expr";
  EXPECT_EQ(c, -1);
  c = 1;
  LOG_IF(INFO, c -= 2) << "log_if info expr";
  EXPECT_EQ(c, -1);
  c = 1;
  LOG_IF(ERROR, c -= 2) << "log_if error expr";
  EXPECT_EQ(c, -1);
  c = 2;
  VLOG_IF(0, c -= 2) << "don't vlog_if 0 expr";
  EXPECT_EQ(c, 0);
  c = 2;
  LOG_IF(ERROR, c -= 2) << "don't log_if error expr";
  EXPECT_EQ(c, 0);

  c = 3;
  LOG_IF_EVERY_N(INFO, c -= 4, 1) << "log_if info every 1 expr";
  EXPECT_EQ(c, -1);
  c = 3;
  LOG_IF_EVERY_N(ERROR, c -= 4, 1) << "log_if error every 1 expr";
  EXPECT_EQ(c, -1);
  c = 4;
  LOG_IF_EVERY_N(ERROR, c -= 4, 3) << "don't log_if info every 3 expr";
  EXPECT_EQ(c, 0);
  c = 4;
  LOG_IF_EVERY_N(ERROR, c -= 4, 3) << "don't log_if error every 3 expr";
  EXPECT_EQ(c, 0);
  c = 5;
  VLOG_IF_EVERY_N(0, c -= 4, 1) << "vlog_if 0 every 1 expr";
  EXPECT_EQ(c, 1);
  c = 5;
  VLOG_IF_EVERY_N(100, c -= 4, 3) << "vlog_if 100 every 3 expr";
  EXPECT_EQ(c, 1);
  c = 6;
  VLOG_IF_EVERY_N(0, c -= 6, 1) << "don't vlog_if 0 every 1 expr";
  EXPECT_EQ(c, 0);
  c = 6;
  VLOG_IF_EVERY_N(100, c -= 6, 3) << "don't vlog_if 100 every 1 expr";
  EXPECT_EQ(c, 0);
}

void TestLoggingLevels() {
  LogWithLevels(0, NGLOG_INFO, false, false);
  LogWithLevels(1, NGLOG_INFO, false, false);
  LogWithLevels(-1, NGLOG_INFO, false, false);
  LogWithLevels(0, NGLOG_WARNING, false, false);
  LogWithLevels(0, NGLOG_ERROR, false, false);
  LogWithLevels(0, NGLOG_FATAL, false, false);
  LogWithLevels(0, NGLOG_FATAL, true, false);
  LogWithLevels(0, NGLOG_FATAL, false, true);
  LogWithLevels(1, NGLOG_WARNING, false, false);
  LogWithLevels(1, NGLOG_FATAL, false, true);
}

int TestVlogHelper() {
  if (VLOG_IS_ON(1)) {
    return 1;
  }
  return 0;
}

void TestVLogModule() {
  int c = TestVlogHelper();
  EXPECT_EQ(0, c);

#if defined(__GNUC__)
  EXPECT_EQ(0, SetVLOGLevel("logging_unittest", 1));
  c = TestVlogHelper();
  EXPECT_EQ(1, c);
#endif
}

#ifdef GTEST_HAS_DEATH_TEST
TEST(DeathRawCHECK, logging) {
  ASSERT_DEATH(RAW_CHECK(false, "failure 1"),
               "RAW: Check false failed: failure 1");
  ASSERT_DEBUG_DEATH(RAW_DCHECK(1 == 2, "failure 2"),
                     "RAW: Check 1 == 2 failed: failure 2");
}
#endif

void TestLogString() {
  vector<string> errors;
  vector<string>* no_errors = nullptr;

  LOG_STRING(INFO, &errors) << "LOG_STRING: " << "collected info";
  LOG_STRING(WARNING, &errors) << "LOG_STRING: " << "collected warning";
  LOG_STRING(ERROR, &errors) << "LOG_STRING: " << "collected error";

  LOG_STRING(INFO, no_errors) << "LOG_STRING: " << "reported info";
  LOG_STRING(WARNING, no_errors) << "LOG_STRING: " << "reported warning";
  LOG_STRING(ERROR, nullptr) << "LOG_STRING: " << "reported error";

  for (auto& error : errors) {
    LOG(INFO) << "Captured by LOG_STRING:  " << error;
  }
}

void TestLogToString() {
  string error;
  string* no_error = nullptr;

  LOG_TO_STRING(INFO, &error) << "LOG_TO_STRING: " << "collected info";
  LOG(INFO) << "Captured by LOG_TO_STRING:  " << error;
  LOG_TO_STRING(WARNING, &error) << "LOG_TO_STRING: " << "collected warning";
  LOG(INFO) << "Captured by LOG_TO_STRING:  " << error;
  LOG_TO_STRING(ERROR, &error) << "LOG_TO_STRING: " << "collected error";
  LOG(INFO) << "Captured by LOG_TO_STRING:  " << error;

  LOG_TO_STRING(INFO, no_error) << "LOG_TO_STRING: " << "reported info";
  LOG_TO_STRING(WARNING, no_error) << "LOG_TO_STRING: " << "reported warning";
  LOG_TO_STRING(ERROR, nullptr) << "LOG_TO_STRING: " << "reported error";
}

class TestLogSinkImpl : public LogSink {
 public:
  vector<string> errors;
  void send(LogSeverity severity, const char* /* full_filename */,
            const char* base_filename, int line,
            const LogMessageTime& logmsgtime, const char* message,
            size_t message_len) override {
    errors.push_back(ToString(severity, base_filename, line, logmsgtime,
                              message, message_len));
  }
};

class BlockingLogger : public base::Logger {
 public:
  void Write(bool /* force_flush */,
             const std::chrono::system_clock::time_point&,
             const char* /* message */, size_t /* length */) override {
    std::unique_lock<std::mutex> lock(mutex_);
    entered_ = true;
    condition_.notify_all();
    condition_.wait(lock, [this] { return released_; });
  }

  void Flush() override {}
  uint32 LogSize() override { return 0; }

  void WaitUntilEntered() {
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait(lock, [this] { return entered_; });
  }

  void Release() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      released_ = true;
    }
    condition_.notify_all();
  }

 private:
  std::mutex mutex_;
  std::condition_variable condition_;
  bool entered_{false};
  bool released_{false};
};

class ReentrantLogSink : public LogSink {
 public:
  // Joining the thread deadlocks if a sink registry lock is held during the
  // callback. CTest reports such a hang by timing out the test.
  void send(LogSeverity /* severity */, const char* /* full_filename */,
            const char* /* base_filename */, int /* line */,
            const LogMessageTime& /* time */, const char* /* message */,
            size_t /* message_len */) override {
    bool expected = false;
    if (!nested_started_.compare_exchange_strong(expected, true)) {
      return;
    }

    std::thread([] { LOG(INFO) << "Nested log from sink callback."; }).join();
    nested_completed_ = true;
  }

  bool NestedCompleted() const { return nested_completed_; }

 private:
  std::atomic<bool> nested_started_{false};
  std::atomic<bool> nested_completed_{false};
};

class SelfRemovingLogSink : public LogSink {
 public:
  void send(LogSeverity, const char*, const char*, int, const LogMessageTime&,
            const char*, size_t) override {
    RemoveLogSink(this);
  }
};

class ReentrantLogger : public base::Logger {
 public:
  void Write(bool, const std::chrono::system_clock::time_point&, const char*,
             size_t) override {
    if (!nested_) {
      nested_ = true;
      LOG(INFO) << "Nested custom logger message.";
    }
  }

  void Flush() override {}
  uint32 LogSize() override { return 0; }

 private:
  bool nested_{false};
};

void TestLogSink() {
  TestLogSinkImpl sink;
  LogSink* no_sink = nullptr;

  LOG_TO_SINK(&sink, INFO) << "LOG_TO_SINK: " << "collected info";
  LOG_TO_SINK(&sink, WARNING) << "LOG_TO_SINK: " << "collected warning";
  LOG_TO_SINK(&sink, ERROR) << "LOG_TO_SINK: " << "collected error";

  LOG_TO_SINK(no_sink, INFO) << "LOG_TO_SINK: " << "reported info";
  LOG_TO_SINK(no_sink, WARNING) << "LOG_TO_SINK: " << "reported warning";
  LOG_TO_SINK(nullptr, ERROR) << "LOG_TO_SINK: " << "reported error";

  LOG_TO_SINK_BUT_NOT_TO_LOGFILE(&sink, INFO)
      << "LOG_TO_SINK_BUT_NOT_TO_LOGFILE: " << "collected info";
  LOG_TO_SINK_BUT_NOT_TO_LOGFILE(&sink, WARNING)
      << "LOG_TO_SINK_BUT_NOT_TO_LOGFILE: " << "collected warning";
  LOG_TO_SINK_BUT_NOT_TO_LOGFILE(&sink, ERROR)
      << "LOG_TO_SINK_BUT_NOT_TO_LOGFILE: " << "collected error";

  LOG_TO_SINK_BUT_NOT_TO_LOGFILE(no_sink, INFO)
      << "LOG_TO_SINK_BUT_NOT_TO_LOGFILE: " << "thrashed info";
  LOG_TO_SINK_BUT_NOT_TO_LOGFILE(no_sink, WARNING)
      << "LOG_TO_SINK_BUT_NOT_TO_LOGFILE: " << "thrashed warning";
  LOG_TO_SINK_BUT_NOT_TO_LOGFILE(nullptr, ERROR)
      << "LOG_TO_SINK_BUT_NOT_TO_LOGFILE: " << "thrashed error";

  LOG(INFO) << "Captured by LOG_TO_SINK:";
  for (auto& error : sink.errors) {
    LogMessage("foo", LogMessage::kNoLogPrefix, NGLOG_INFO).stream() << error;
  }
}

TEST(Logging, RegisteredSinkCanLogDuringSend) {
  ReentrantLogSink sink;
  AddLogSink(&sink);

  LOG(INFO) << "Log to registered reentrant sink.";

  RemoveLogSink(&sink);
  EXPECT_TRUE(sink.NestedCompleted());
}

TEST(Logging, RegisteredSinkCanRemoveItself) {
#if defined(HAVE_SYS_WAIT_H) && defined(HAVE_UNISTD_H) && \
    defined(GTEST_HAS_DEATH_TEST)
  constexpr unsigned kDeathTestTimeoutSeconds = 2;
  ASSERT_EXIT(
      {
        alarm(kDeathTestTimeoutSeconds);
        SelfRemovingLogSink sink;
        AddLogSink(&sink);
        LOG(INFO) << "Remove sink from callback.";
        _exit(EXIT_SUCCESS);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
#endif
}

TEST(Logging, CustomLoggerCanLogDuringWrite) {
#if defined(HAVE_SYS_WAIT_H) && defined(HAVE_UNISTD_H) && \
    defined(GTEST_HAS_DEATH_TEST)
  constexpr unsigned kDeathTestTimeoutSeconds = 2;
  ASSERT_EXIT(
      {
        alarm(kDeathTestTimeoutSeconds);
        nglog::SetFlag(FLAGS_logtostderr, false);
        nglog::SetFlag(FLAGS_logtostdout, false);
        auto logger = std::make_unique<ReentrantLogger>();
        base::SetLogger(NGLOG_INFO, logger.release());
        LOG(INFO) << "Log through a reentrant custom logger.";
        _exit(EXIT_SUCCESS);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
#endif
}

TEST(Logging, DirectSinkCanLogDuringSend) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, true);

  ReentrantLogSink sink;
  LOG_TO_SINK_BUT_NOT_TO_LOGFILE(&sink, INFO)
      << "Log to direct reentrant sink.";

  EXPECT_TRUE(sink.NestedCompleted());
}

#ifdef NGLOG_ENABLE_LOCK_METRICS
TEST(Logging, DirectSinkWithoutRegisteredSinksDoesNotLockSinkRegistry) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, true);
  internal::ResetLockMetrics();

  TestLogSinkImpl sink;
  LOG_TO_SINK_BUT_NOT_TO_LOGFILE(&sink, INFO)
      << "Log without registered sinks.";

  const internal::LockMetrics metrics =
      internal::GetLockMetrics(internal::LockKind::kSink);
  EXPECT_EQ(metrics.acquisitions, 0U);
  EXPECT_EQ(internal::GetLockMetrics(internal::LockKind::kLog).acquisitions,
            0U);
}

TEST(Logging, RegisteredSinkDispatchUsesOneRegistrySnapshot) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, true);
  TestLogSinkImpl sink;
  AddLogSink(&sink);
  internal::ResetLockMetrics();

  LOG(INFO) << "Log to one registered sink.";

  const internal::LockMetrics metrics =
      internal::GetLockMetrics(internal::LockKind::kSink);
  RemoveLogSink(&sink);
  EXPECT_EQ(metrics.acquisitions, 1U);
}

#endif

class MessageCollectingSink : public LogSink {
 public:
  vector<string> messages;
  void send(LogSeverity /*severity*/, const char* /*full_filename*/,
            const char* /*base_filename*/, int /*line*/,
            const LogMessageTime& /*time*/, const char* message,
            size_t message_len) override {
    messages.emplace_back(message, message_len);
  }
};

TEST(Logging, EmptyMessageWithoutPrefixIsSentAsEmptyMessage) {
  MessageCollectingSink sink;
  {
    LogMessage message(__FILE__, LogMessage::kNoLogPrefix, NGLOG_INFO, &sink,
                       /*also_send_to_log=*/false);
  }
  EXPECT_THAT(sink.messages, testing::ElementsAre(""));
}

#if defined(NGLOG_OS_LINUX)
TEST(Logging, ConcurrentFullDiskWrites) {
  constexpr int kFullDiskWriteCount = 100;
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, false);
  nglog::SetFlag(FLAGS_logtostdout, false);
  nglog::SetFlag(FLAGS_log_file_header, false);
  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, false);
  nglog::SetFlag(FLAGS_stop_logging_if_full_disk, true);
  SetLogDestination(NGLOG_INFO, "/dev/full");
  SetLogDestination(NGLOG_WARNING, "/dev/full");

  base::Logger* info_logger = base::GetLogger(NGLOG_INFO);
  base::Logger* warning_logger = base::GetLogger(NGLOG_WARNING);
  const auto write_logs = [](base::Logger* logger) {
    const auto timestamp = std::chrono::system_clock::now();
    for (int i = 0; i < kFullDiskWriteCount; ++i) {
      logger->Write(false, timestamp, "x", 1);
    }
  };
  std::thread info_thread{write_logs, info_logger};
  std::thread warning_thread{write_logs, warning_logger};
  info_thread.join();
  warning_thread.join();

  LogToStderr();
}
#endif

#if !defined(NGLOG_OS_WINDOWS)
TEST(Logging, ReprintFatalMessageDoesNotColorStderr) {
  const char* old_clicolor_force = std::getenv("CLICOLOR_FORCE");
  const bool had_clicolor_force = old_clicolor_force != nullptr;
  const std::string saved_clicolor_force =
      had_clicolor_force ? old_clicolor_force : "";
  const char* old_no_color = std::getenv("NO_COLOR");
  const bool had_no_color = old_no_color != nullptr;
  const std::string saved_no_color = had_no_color ? old_no_color : "";
  auto restore_environment = [had_clicolor_force, saved_clicolor_force,
                              had_no_color, saved_no_color] {
    if (had_clicolor_force) {
      setenv("CLICOLOR_FORCE", saved_clicolor_force.c_str(), 1);
    } else {
      unsetenv("CLICOLOR_FORCE");
    }
    if (had_no_color) {
      setenv("NO_COLOR", saved_no_color.c_str(), 1);
    } else {
      unsetenv("NO_COLOR");
    }
  };
  ScopedExit<decltype(restore_environment)> restore{restore_environment};

  ASSERT_EQ(setenv("CLICOLOR_FORCE", "1", 1), 0);
  unsetenv("NO_COLOR");

  auto flags = nglog::MakeScopedFlags(
      nglog::MakeScopedFlag(FLAGS_logtostderr, true),
      nglog::MakeScopedFlag(FLAGS_logtostdout, false),
      nglog::MakeScopedFlag(FLAGS_colorlogtostderr, true));
  const bool previous_exit_on_dfatal = base::internal::GetExitOnDFatal();
  base::internal::SetExitOnDFatal(true);
  auto restore_exit_on_dfatal = [previous_exit_on_dfatal] {
    base::internal::SetExitOnDFatal(previous_exit_on_dfatal);
  };
  ScopedExit<decltype(restore_exit_on_dfatal)> restore_exit{
      restore_exit_on_dfatal};

  const logging_fail_func_t previous_failure_function =
      InstallFailureFunction(&ThrowFatalLogFailure);
  auto restore_failure_function = [previous_failure_function] {
    InstallFailureFunction(previous_failure_function);
  };
  ScopedExit<decltype(restore_failure_function)> restore_failure{
      restore_failure_function};

  CaptureTestStderr();
  EXPECT_THROW({ LOG(FATAL) << "fatal message for replay"; }, std::logic_error);
  const std::string initial_output = GetCapturedTestStderr();
  ASSERT_NE(initial_output.find("\033["), std::string::npos);

  CaptureTestStderr();
  ReprintFatalMessage();
  const std::string replayed_output = GetCapturedTestStderr();
  EXPECT_EQ(replayed_output.find("\033["), std::string::npos);
}
#endif

TEST(Logging, ConcurrentFatalMessages) {
  constexpr int kFatalMessageCount = 20;
  const logging_fail_func_t previous_failure_function =
      InstallFailureFunction(&ThrowFatalLogFailure);
  const bool previous_exit_on_dfatal = base::internal::GetExitOnDFatal();
  base::internal::SetExitOnDFatal(true);

  const auto write_fatal = [] {
    for (int i = 0; i < kFatalMessageCount; ++i) {
      try {
        LOG(FATAL) << "Concurrent fatal message.";
      } catch (const std::logic_error&) {
      }
    }
  };
  std::thread first{write_fatal};
  std::thread second{write_fatal};
  first.join();
  second.join();

  base::internal::SetExitOnDFatal(previous_exit_on_dfatal);
  InstallFailureFunction(previous_failure_function);
}

// For testing using CHECK*() on anonymous enums.
enum { CASE_A, CASE_B };

void TestCHECK() {
  // Tests using CHECK*() on int values.
  CHECK(1 == 1);
  CHECK_EQ(1, 1);
  CHECK_NE(1, 2);
  CHECK_GE(1, 1);
  CHECK_GE(2, 1);
  CHECK_LE(1, 1);
  CHECK_LE(1, 2);
  CHECK_GT(2, 1);
  CHECK_LT(1, 2);

  // Tests using CHECK*() on anonymous enums.
  // Apple's GCC doesn't like this.
#if !defined(NGLOG_OS_MACOSX)
  CHECK_EQ(CASE_A, CASE_A);
  CHECK_NE(CASE_A, CASE_B);
  CHECK_GE(CASE_A, CASE_A);
  CHECK_GE(CASE_B, CASE_A);
  CHECK_LE(CASE_A, CASE_A);
  CHECK_LE(CASE_A, CASE_B);
  CHECK_GT(CASE_B, CASE_A);
  CHECK_LT(CASE_A, CASE_B);
#endif
}

void TestDCHECK() {
#if defined(NDEBUG)
  DCHECK(1 == 2) << " DCHECK's shouldn't be compiled in normal mode";
#endif
  DCHECK(1 == 1);
  DCHECK_EQ(1, 1);
  DCHECK_NE(1, 2);
  DCHECK_GE(1, 1);
  DCHECK_GE(2, 1);
  DCHECK_LE(1, 1);
  DCHECK_LE(1, 2);
  DCHECK_GT(2, 1);
  DCHECK_LT(1, 2);

  auto orig_ptr = std::make_unique<int64>();
  int64* ptr = DCHECK_NOTNULL(orig_ptr.get());
  CHECK_EQ(ptr, orig_ptr.get());
}

void TestSTREQ() {
  CHECK_STREQ("this", "this");
  CHECK_STREQ(nullptr, nullptr);
  CHECK_STRCASEEQ("this", "tHiS");
  CHECK_STRCASEEQ(nullptr, nullptr);
  CHECK_STRNE("this", "tHiS");
  CHECK_STRNE("this", nullptr);
  CHECK_STRCASENE("this", "that");
  CHECK_STRCASENE(nullptr, "that");
  CHECK_STREQ((string("a") + "b").c_str(), "ab");
  CHECK_STREQ(string("test").c_str(), (string("te") + string("st")).c_str());
}

#ifdef GTEST_HAS_DEATH_TEST
TEST(DeathSTREQ, logging) {
  ASSERT_DEATH(CHECK_STREQ(nullptr, "this"), "");
  ASSERT_DEATH(CHECK_STREQ("this", "siht"), "");
  ASSERT_DEATH(CHECK_STRCASEEQ(nullptr, "siht"), "");
  ASSERT_DEATH(CHECK_STRCASEEQ("this", "siht"), "");
  ASSERT_DEATH(CHECK_STRNE(nullptr, nullptr), "");
  ASSERT_DEATH(CHECK_STRNE("this", "this"), "");
  ASSERT_DEATH(CHECK_STREQ((string("a") + "b").c_str(), "abc"), "");
}
#endif

TEST(CheckNOTNULL, Simple) {
  int64 t;
  void* ptr = static_cast<void*>(&t);
  void* ref = CHECK_NOTNULL(ptr);
  EXPECT_EQ(ptr, ref);
  CHECK_NOTNULL(reinterpret_cast<char*>(ptr));
  CHECK_NOTNULL(reinterpret_cast<unsigned char*>(ptr));
  CHECK_NOTNULL(reinterpret_cast<int*>(ptr));
  CHECK_NOTNULL(reinterpret_cast<int64*>(ptr));
}

#ifdef GTEST_HAS_DEATH_TEST
TEST(DeathCheckNN, Simple) {
  ASSERT_DEATH(CHECK_NOTNULL(static_cast<void*>(nullptr)), "");
}
#endif

// Get list of file names that match pattern
static void GetFiles(const string& pattern, vector<string>* files) {
  files->clear();
#if defined(HAVE_GLOB_H)
  glob_t g{};
  const auto cleanup_fn = [&g] { globfree(&g); };
  const ScopedExit<decltype(cleanup_fn)> cleanup{cleanup_fn};
  const int r = glob(pattern.c_str(), 0, nullptr, &g);
  CHECK((r == 0) || (r == GLOB_NOMATCH)) << ": error matching " << pattern;
  for (size_t i = 0; i < g.gl_pathc; i++) {
    files->push_back(string(g.gl_pathv[i]));
  }
#elif defined(NGLOG_OS_WINDOWS)
  std::wstring wide_pattern;
  CHECK(nglog::internal::Utf8ToWide(pattern.data(), pattern.size(),
                                    &wide_pattern));
  WIN32_FIND_DATAW data;
  WindowsHandle handle{FindFirstFileW(wide_pattern.c_str(), &data)};
  size_t index = pattern.rfind('\\');
  if (index == string::npos) {
    LOG(FATAL) << "No directory separator.";
  }
  const string dirname = pattern.substr(0, index + 1);
  if (handle == nullptr || handle.get() == INVALID_HANDLE_VALUE) {
    // Finding no files is OK.
    return;
  }
  do {
    std::string filename;
    const std::size_t filename_length = std::wcslen(data.cFileName);
    CHECK(nglog::internal::WideToUtf8(data.cFileName, filename_length,
                                      &filename));
    files->push_back(dirname + filename);
  } while (FindNextFileW(handle.get(), &data));
#else
#  error There is no way to do glob.
#endif
}

// Delete files patching pattern
static void DeleteFiles(const string& pattern) {
  vector<string> files;
  GetFiles(pattern, &files);
  for (auto& file : files) {
    CHECK(unlink(file.c_str()) == 0) << ": " << strerror(errno);
  }
}

TEST(Logging, DeleteFilesAcceptsNoMatches) {
  DeleteFiles(TestTmpDir() + "/nglog-file-that-does-not-exist-*");
}

#ifndef NGLOG_OS_WINDOWS
// Other processes create and delete files in the system temporary directory
// at any time. Emscripten ends a directory listing early when one of its
// entries vanishes, so test files must not be listed in a shared directory.
TEST(Logging, ListingIsUnaffectedBySharedTemporaryDirectory) {
  constexpr int kFillerCount = 128;
  constexpr int kAttempts = 1000;

  const std::string target = TestTmpDir() + "/listing_target";
  FileDescriptor target_file{
      open(target.c_str(), O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR)};
  ASSERT_TRUE(target_file) << strerror(errno);
  target_file.reset();
  const auto remove_target = [&target] { unlink(target.c_str()); };
  const ScopedExit<decltype(remove_target)> target_cleanup{remove_target};

  std::vector<std::string> fillers;
  fillers.reserve(kFillerCount);
  for (int i = 0; i < kFillerCount; ++i) {
    fillers.push_back(SystemTmpDir() + "/nglog_listing_filler_" +
                      std::to_string(getpid()) + '_' + std::to_string(i));
  }
  const auto remove_fillers = [&fillers] {
    for (const std::string& filler : fillers) {
      unlink(filler.c_str());
    }
  };
  const ScopedExit<decltype(remove_fillers)> fillers_cleanup{remove_fillers};

  std::atomic<bool> done{false};
  std::thread churn{[&done, &fillers] {
    while (!done.load(std::memory_order_relaxed)) {
      for (const std::string& filler : fillers) {
        FileDescriptor{
            open(filler.c_str(), O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR)};
      }
      for (const std::string& filler : fillers) {
        unlink(filler.c_str());
      }
    }
  }};
  const auto stop_churn = [&done, &churn] {
    done.store(true, std::memory_order_relaxed);
    churn.join();
  };
  const ScopedExit<decltype(stop_churn)> churn_cleanup{stop_churn};

  int missed = 0;
  for (int i = 0; i < kAttempts; ++i) {
    vector<string> files;
    GetFiles(target + "*", &files);
    if (files.size() != 1) {
      ++missed;
    }
  }

  EXPECT_EQ(missed, 0) << "out of " << kAttempts << " listings";
}
#endif

// check string is in file (or is *NOT*, depending on optional checkInFileOrNot)
static void CheckFile(const string& name, const string& expected_string,
                      const bool checkInFileOrNot = true) {
  vector<string> files;
  GetFiles(name + "*", &files);
  CHECK_EQ(files.size(), 1UL);

  std::unique_ptr<std::FILE> file{fopen(files[0].c_str(), "r")};
  CHECK(file != nullptr) << ": could not open " << files[0];
  char buf[1000];
  while (fgets(buf, sizeof(buf), file.get()) != nullptr) {
    char* first = strstr(buf, expected_string.c_str());
    // if first == nullptr, not found.
    // Terser than if (checkInFileOrNot && first != nullptr || !check...
    if (checkInFileOrNot != (first == nullptr)) {
      return;
    }
  }
  LOG(FATAL) << "Did " << (checkInFileOrNot ? "not " : "") << "find "
             << expected_string << " in " << files[0];
}

// Check that a string is not contained anywhere in a file
static void CheckNotInFile(const string& name, const string& unexpected) {
  vector<string> files;
  GetFiles(name + "*", &files);
  CHECK_EQ(files.size(), 1UL);

  std::unique_ptr<std::FILE> file{fopen(files[0].c_str(), "r")};
  CHECK(file != nullptr) << ": could not open " << files[0];
  const string content = ReadEntireFile(file.get());
  CHECK(content.find(unexpected) == string::npos)
      << ": found " << unexpected << " in " << files[0];
}

TEST(ScopedFlags, MoveTransfersRestoration) {
  const bool original = nglog::GetFlag(FLAGS_logtostderr);

  {
    auto save = nglog::MakeScopedFlags(
        nglog::MakeScopedFlag(FLAGS_logtostderr, !original));
    auto moved_save = std::move(save);
    EXPECT_EQ(!original, nglog::GetFlag(FLAGS_logtostderr));
  }

  EXPECT_EQ(original, nglog::GetFlag(FLAGS_logtostderr));
}

TEST(ScopedFlags, EmptyScopeRestoresAllFlags) {
  const bool original = nglog::GetFlag(FLAGS_logtostderr);

  {
    nglog::ScopedFlags<> save;
    nglog::SetFlag(FLAGS_logtostderr, !original);
    EXPECT_EQ(!original, nglog::GetFlag(FLAGS_logtostderr));
  }

  EXPECT_EQ(original, nglog::GetFlag(FLAGS_logtostderr));
}

TEST(Logging, MaxLogSizeWhenNoTimestamp) {
  // SetLogDestination() is a no-op while FLAGS_logtostderr is true.
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, false);

  fprintf(stderr, "==== Test setting max log size without timestamp\n");
  const string dest = TestTmpDir() + "/logging_test_max_log_size";
  DeleteFiles(dest + "*");

  // Set max log size to 1MB.
  const auto original_max_log_size = nglog::GetFlag(FLAGS_max_log_size);
  nglog::SetFlag(FLAGS_max_log_size, 1U);
  const auto original_timestamp_in_logfile_name =
      nglog::GetFlag(FLAGS_timestamp_in_logfile_name);
  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, false);

  // Set log destination
  SetLogDestination(NGLOG_INFO, dest.c_str());

  // 1e4 info logs -> is about 772 KB in size
  // 2e4 info logs -> is around 1500 KB in size -> 1.5MB
  // If our max_log_size constraint is respected, it will truncate earlier logs
  // and the file size will be lesser than 1MB (around 0.5MB)
  const int num_logs = 2e4;
  for (int i = 0; i < num_logs; i++) {
    LOG(INFO) << "Hello world";
  }
  FlushLogFiles(NGLOG_INFO);

  // Close the destination file before checking its size: on Windows, a
  // stat() of a file that still has an open write handle in this same
  // process can report a stale (pre-flush) size, even after fflush().
  LogToStderr();

  // Check log file size
  struct stat statbuf;
  CHECK_ERR(stat(dest.c_str(), &statbuf))
      << ": failed to determine size of log file " << dest;

  // Verify file size is less than the max log size limit
  CHECK_LT(static_cast<std::size_t>(statbuf.st_size),
           nglog::GetFlag(FLAGS_max_log_size) << 20U);

  // Reset flag values to their original values
  nglog::SetFlag(FLAGS_max_log_size, original_max_log_size);
  nglog::SetFlag(FLAGS_timestamp_in_logfile_name,
                 original_timestamp_in_logfile_name);

  DeleteFiles(dest + "*");
}

TEST(Logging, MaxLogSizeAboveCapNotFlooredToMinimum) {
  // SetLogDestination() is a no-op while FLAGS_logtostderr is true.
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, false);

  fprintf(stderr,
          "==== Test max log size above the cap is not floored to 1MB\n");
  // Deliberately not a "logging_test_max_log_size*"-prefixed name: that
  // wildcard is also DeleteFiles()'s cleanup pattern in
  // MaxLogSizeWhenNoTimestamp above, and would match this test's file too.
  const string dest = TestTmpDir() + "/logging_test_uncapped_max_log_size";
  DeleteFiles(dest + "*");

  const auto original_max_log_size = nglog::GetFlag(FLAGS_max_log_size);
  const auto original_timestamp_in_logfile_name =
      nglog::GetFlag(FLAGS_timestamp_in_logfile_name);

  // A value larger than the 4095MB cap used to be clamped to the 1MB minimum,
  // which would rotate the file after ~1MB. It must instead be capped to the
  // maximum, so the file is allowed to grow past 1MB without rotating.
  nglog::SetFlag(FLAGS_max_log_size, 8000U);
  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, false);

  SetLogDestination(NGLOG_INFO, dest.c_str());

  // 20000 info logs -> around 1.5MB, i.e. comfortably above 1MB. If the
  // oversized limit were (incorrectly) floored to 1MB, earlier logs would be
  // truncated and the resulting file would stay below 1MB.
  constexpr int num_logs = 20'000;
  for (int i = 0; i < num_logs; i++) {
    LOG(INFO) << "Hello world";
  }
  FlushLogFiles(NGLOG_INFO);

  // Close the destination file before checking its size: on Windows, a
  // stat() of a file that still has an open write handle in this same
  // process can report a stale (pre-flush) size, even after fflush().
  LogToStderr();

  struct stat statbuf;
  CHECK_ERR(stat(dest.c_str(), &statbuf))
      << ": failed to determine size of log file " << dest;

  // No rotation at 1MB should have happened, so the file exceeds 1MB.
  CHECK_GT(static_cast<std::size_t>(statbuf.st_size), 1U << 20U);

  nglog::SetFlag(FLAGS_max_log_size, original_max_log_size);
  nglog::SetFlag(FLAGS_timestamp_in_logfile_name,
                 original_timestamp_in_logfile_name);

  DeleteFiles(dest + "*");
}

TEST(Logging, Basename) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, false);

  fprintf(stderr, "==== Test setting log file basename\n");
  const string dest = TestTmpDir() + "/logging_test_basename";
  DeleteFiles(dest + "*");

  SetLogDestination(NGLOG_INFO, dest.c_str());
  LOG(INFO) << "message to new base";
  FlushLogFiles(NGLOG_INFO);

  CheckFile(dest, "message to new base");

  // Release file handle for the destination file to unlock the file in Windows.
  LogToStderr();
  DeleteFiles(dest + "*");
}

#ifdef HAVE_POSIX_LONG_PATH_SUPPORT
TEST(Logging, LongPosixPath) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, false);
  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, false);

  PosixLongPathTestDirectory directory;
  const std::string directory_name =
      "nglog_posix_long_path_" + std::to_string(getpid());
  ASSERT_TRUE(directory.CreateInTemporaryDirectory(directory_name));
  constexpr char kFilename[] = "logging_test_long_path";
  const std::string destination = directory.ChildPath(kFilename);

  SetLogDestination(NGLOG_INFO, destination.c_str());
  LOG(INFO) << "message in long POSIX path";
  FlushLogFiles(NGLOG_INFO);
  LogToStderr();

  EXPECT_THAT(directory.ReadChild(kFilename),
              HasSubstr("message in long POSIX path"));
}

TEST(PosixPathOperations, LongRelativePath) {
  PosixLongPathTestDirectory directory;
  const std::string directory_name =
      "nglog_posix_path_operations_" + std::to_string(getpid());
  ASSERT_TRUE(directory.CreateInCurrentDirectory(directory_name));

  constexpr char kFilename[] = "path_operations_file";
  constexpr char kLinkName[] = "path_operations_link";
  constexpr char kContents[] = "long path contents";
  const std::string filename = directory.ChildPath(kFilename);
  const std::string link_path = directory.ChildPath(kLinkName);
  constexpr int kFileMode = S_IRUSR | S_IWUSR;
  FileDescriptor fd{internal::OpenPosixPath(
      filename, O_CREAT | O_WRONLY | O_TRUNC, kFileMode)};
  ASSERT_TRUE(fd);
  ASSERT_EQ(write(fd.get(), kContents, sizeof(kContents) - 1),
            static_cast<ssize_t>(sizeof(kContents) - 1));
  fd.reset();

  struct stat status;
  ASSERT_EQ(internal::StatPosixPath(filename, &status), 0);
  EXPECT_EQ(status.st_size, static_cast<off_t>(sizeof(kContents) - 1));
  EXPECT_EQ(internal::AccessPosixPath(filename, F_OK), 0);

  std::vector<std::string> entries;
  ASSERT_TRUE(internal::ListPosixDirectory(directory.path(), &entries));
  EXPECT_THAT(entries, ::testing::Contains(kFilename));
  EXPECT_THAT(entries, ::testing::Not(::testing::Contains(".")));
  EXPECT_THAT(entries, ::testing::Not(::testing::Contains("..")));

  ASSERT_EQ(
      internal::SymlinkPosixPath(kFilename, sizeof(kFilename) - 1, link_path),
      0);
  ASSERT_EQ(internal::StatPosixPath(link_path, &status), 0);
  EXPECT_EQ(status.st_size, static_cast<off_t>(sizeof(kContents) - 1));

  ASSERT_EQ(internal::TruncatePosixPath(filename), 0);
  ASSERT_EQ(internal::StatPosixPath(filename, &status), 0);
  EXPECT_EQ(status.st_size, 0);

  EXPECT_EQ(internal::UnlinkPosixPath(link_path), 0);
  EXPECT_EQ(internal::UnlinkPosixPath(filename), 0);

  std::string invalid_path = directory.ChildPath("invalid");
  invalid_path.push_back('\0');
  invalid_path += "suffix";
  errno = 0;
  EXPECT_EQ(internal::OpenPosixPath(invalid_path, O_RDONLY, 0), -1);
  EXPECT_EQ(errno, EINVAL);
}
#endif

TEST(Logging, LogfileMode) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, false);

#ifdef NGLOG_OS_WINDOWS
  constexpr std::uint32_t kLogfileMode = _S_IREAD | _S_IWRITE;
#else
  constexpr std::uint32_t kLogfileMode = S_IRUSR | S_IWUSR;
#endif
  nglog::SetFlag(FLAGS_logfile_mode, nglog::FileMode{kLogfileMode});
  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, false);

  const string dest = TestTmpDir() + "/logging_test_file_mode";
  DeleteFiles(dest + "*");

  SetLogDestination(NGLOG_INFO, dest.c_str());
  LOG(INFO) << "message with configured logfile mode";
  FlushLogFiles(NGLOG_INFO);

  CheckFile(dest, "message with configured logfile mode");

  LogToStderr();
  DeleteFiles(dest + "*");
}

TEST(Logging, BasenameAppendWhenNoTimestamp) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, false);

  fprintf(stderr,
          "==== Test setting log file basename without timestamp and appending "
          "properly\n");
  const string dest =
      TestTmpDir() + "/logging_test_basename_append_when_no_timestamp";
  DeleteFiles(dest + "*");

  ofstream out(dest.c_str());
  out << "test preexisting content" << endl;
  out.close();

  CheckFile(dest, "test preexisting content");

  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, false);
  SetLogDestination(NGLOG_INFO, dest.c_str());
  LOG(INFO) << "message to new base, appending to preexisting file";
  FlushLogFiles(NGLOG_INFO);
  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, true);

  // if the logging overwrites the file instead of appending it will fail.
  CheckFile(dest, "test preexisting content");
  CheckFile(dest, "message to new base, appending to preexisting file");

  // Release file handle for the destination file to unlock the file in Windows.
  LogToStderr();
  DeleteFiles(dest + "*");
}

TEST(Logging, HeaderFormatLineWithCustomPrefixFormatter) {
  // SetLogDestination() is a no-op while FLAGS_logtostderr is true.
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, false);

  fprintf(stderr, "==== Test log file header format line\n");
  const string dest = TestTmpDir() + "/logging_test_header_format_line";
  DeleteFiles(dest + "*");

  const bool saved_timestamp_in_logfile_name =
      nglog::GetFlag(FLAGS_timestamp_in_logfile_name);
  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, false);

  // A custom prefix formatter is installed (see main). It determines the
  // actual log line format, so the header must not advertise the default
  // format.
  SetLogDestination(NGLOG_INFO, dest.c_str());
  LOG(INFO) << "header with custom prefix formatter";
  FlushLogFiles(NGLOG_INFO);
  CheckFile(dest, "Running duration (h:mm:ss):");  // the header was written
  CheckNotInFile(dest, "Log line format: ");

  // Release file handle for the destination file to unlock the file in Windows.
  LogToStderr();
  DeleteFiles(dest + "*");

  // Without a custom prefix formatter the header advertises the default
  // format.
  InstallPrefixFormatter(nullptr);
  SetLogDestination(NGLOG_INFO, dest.c_str());
  LOG(INFO) << "header with default prefix";
  FlushLogFiles(NGLOG_INFO);
  CheckFile(dest, "Log line format: [IWEF]");

  // Restore the prefix formatter installed by main.
  static string prefix_attacher_data = "good data";
  InstallPrefixFormatter(&PrefixAttacher, &prefix_attacher_data);

  nglog::SetFlag(FLAGS_timestamp_in_logfile_name,
                 saved_timestamp_in_logfile_name);

  // Release file handle for the destination file to unlock the file in Windows.
  LogToStderr();
  DeleteFiles(dest + "*");
}

TEST(Logging, TwoProcessesWrite) {
// The implementation relies on advisory file locking. Emscripten cannot fork.
#if defined(HAVE_SYS_WAIT_H) && defined(HAVE_UNISTD_H) && \
    defined(HAVE_FCNTL) && !defined(NGLOG_OS_EMSCRIPTEN)
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, false);

  fprintf(stderr,
          "==== Test setting log file basename and two processes writing - "
          "second should fail\n");
  const std::string dest =
      TestTmpDir() + "/logging_test_basename_two_processes_writing";
  DeleteFiles(dest + "*");

  // make both processes write into the same file (easier test)
  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, false);
  SetLogDestination(NGLOG_INFO, dest.c_str());
  LOG(INFO) << "message to new base, parent";
  FlushLogFiles(NGLOG_INFO);

  CaptureTestStderr();
  pid_t pid = fork();
  CHECK_ERR(pid);
  if (pid == 0) {
    LOG(INFO) << "message to new base, child - should only appear on STDERR "
                 "not on the file";
    ShutdownLogging();  // for children proc
    exit(EXIT_SUCCESS);
  } else if (pid > 0) {
    wait(nullptr);
  }
  const std::string stderr_output = GetCapturedTestStderr();
  std::istringstream stderr_stream{stderr_output};
  std::string error_line;
  ASSERT_TRUE(std::getline(stderr_stream, error_line));
  std::string extra_line;
  EXPECT_FALSE(std::getline(stderr_stream, extra_line));
  EXPECT_THAT(error_line,
              testing::ContainsRegex("^Could not create log file '[^']+': "));
  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, true);

  CheckFile(dest, "message to new base, parent");
  CheckFile(dest,
            "message to new base, child - should only appear on STDERR not on "
            "the file",
            false);

  // Release
  LogToStderr();
  DeleteFiles(dest + "*");
#elif defined(NGLOG_OS_WINDOWS)
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, false);

  fprintf(stderr,
          "==== Test setting log file basename and two processes writing - "
          "second should fail\n");
  const std::string dest =
      TestTmpDir() + "/logging_test_basename_two_processes_writing";
  DeleteFiles(dest + "*");

  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, false);
  SetLogDestination(NGLOG_INFO, dest.c_str());
  LOG(INFO) << "message to new base, parent";
  FlushLogFiles(NGLOG_INFO);

  std::string command =
      '"' + std::string(g_argv0) + "\" --nglog-log-child \"" + dest + '"';
  std::vector<char> command_line(command.begin(), command.end());
  command_line.push_back('\0');

  STARTUPINFOA startup_info = {};
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process_info = {};
  ASSERT_TRUE(CreateProcessA(nullptr, command_line.data(), nullptr, nullptr,
                             FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                             &startup_info, &process_info));
  WindowsHandle process_handle{process_info.hProcess};
  WindowsHandle thread_handle{process_info.hThread};

  constexpr DWORD kProcessWaitTimeoutMilliseconds = 10'000;
  EXPECT_EQ(WaitForSingleObject(process_handle.get(),
                                kProcessWaitTimeoutMilliseconds),
            WAIT_OBJECT_0);
  DWORD exit_code = 0;
  EXPECT_TRUE(GetExitCodeProcess(process_handle.get(), &exit_code));
  EXPECT_EQ(exit_code, 0U);

  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, true);
  CheckFile(dest, "message to new base, parent");
  CheckFile(dest,
            "message to new base, child - should only appear on STDERR not on "
            "the file",
            false);

  LogToStderr();
  DeleteFiles(dest + "*");
#endif
}

TEST(Logging, FileLockIsHeldAcrossProcesses) {
#ifdef NGLOG_OS_WINDOWS
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, false);

  const std::string dest =
      TestTmpDir() + "/logging_test_file_lock_across_processes";
  const std::string ready = dest + ".ready";
  DeleteFiles(dest + "*");

  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, false);
  SetLogDestination(NGLOG_INFO, dest.c_str());

  std::string command = '"' + std::string(g_argv0) + "\" --nglog-log-child \"" +
                        dest + "\" \"" + ready + '"';
  std::vector<char> command_line(command.begin(), command.end());
  command_line.push_back('\0');

  STARTUPINFOA startup_info = {};
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process_info = {};
  ASSERT_TRUE(CreateProcessA(nullptr, command_line.data(), nullptr, nullptr,
                             FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                             &startup_info, &process_info));
  WindowsHandle process_handle{process_info.hProcess};
  WindowsHandle thread_handle{process_info.hThread};

  constexpr DWORD kReadyWaitMilliseconds = 10'000;
  constexpr DWORD kReadyPollMilliseconds = 10;
  bool ready_seen = false;
  for (DWORD waited = 0; waited < kReadyWaitMilliseconds;
       waited += kReadyPollMilliseconds) {
    if (GetFileAttributesA(ready.c_str()) != INVALID_FILE_ATTRIBUTES) {
      ready_seen = true;
      break;
    }
    Sleep(kReadyPollMilliseconds);
  }
  ASSERT_TRUE(ready_seen);

  WindowsHandle file_handle{
      CreateFileA(dest.c_str(), GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
  ASSERT_NE(file_handle.get(), INVALID_HANDLE_VALUE);

  OVERLAPPED overlapped = {};
  overlapped.Offset = static_cast<DWORD>(kLogFileLockOffset);
  overlapped.OffsetHigh =
      static_cast<DWORD>(kLogFileLockOffset >> kWindowsDwordBits);
  const BOOL lock_succeeded = LockFileEx(
      file_handle.get(), LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0,
      kLogFileLockLength, 0, &overlapped);
  EXPECT_FALSE(lock_succeeded);
  if (lock_succeeded) {
    EXPECT_TRUE(
        UnlockFileEx(file_handle.get(), 0, kLogFileLockLength, 0, &overlapped));
  }

  EXPECT_TRUE(TerminateProcess(process_handle.get(), 0));
  EXPECT_EQ(WaitForSingleObject(process_handle.get(), kReadyWaitMilliseconds),
            WAIT_OBJECT_0);

  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, true);
  file_handle.reset();
  LogToStderr();
  DeleteFiles(dest + "*");
#endif
}

TEST(Logging, LockedOversizedFileIsNotTruncated) {
#ifdef NGLOG_OS_WINDOWS
  auto save = nglog::MakeScopedFlags(
      nglog::MakeScopedFlag(FLAGS_logtostderr, false),
      nglog::MakeScopedFlag(FLAGS_timestamp_in_logfile_name, false),
      nglog::MakeScopedFlag(FLAGS_max_log_size, 1U));

  const std::string dest = TestTmpDir() + "/logging_test_locked_oversized_file";
  const std::string ready = dest + ".ready";
  const std::string preserved_prefix = "preserved logfile contents";
  constexpr std::size_t kMegabyte = 1U << 20U;
  constexpr std::size_t kOversizedFileSize = kMegabyte + 1U;
  DeleteFiles(dest + "*");

  std::string original_contents(kOversizedFileSize, 'x');
  original_contents.replace(0, preserved_prefix.size(), preserved_prefix);
  std::ofstream output{dest, std::ios::binary};
  output.write(original_contents.data(),
               static_cast<std::streamsize>(original_contents.size()));
  output.close();

  std::string command = '"' + std::string(g_argv0) +
                        "\" --nglog-hold-log-lock \"" + dest + "\" \"" + ready +
                        '"';
  std::vector<char> command_line(command.begin(), command.end());
  command_line.push_back('\0');

  STARTUPINFOA startup_info = {};
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process_info = {};
  ASSERT_TRUE(CreateProcessA(nullptr, command_line.data(), nullptr, nullptr,
                             FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                             &startup_info, &process_info));
  WindowsHandle process_handle{process_info.hProcess};
  WindowsHandle thread_handle{process_info.hThread};

  constexpr DWORD kReadyWaitMilliseconds = 10'000;
  constexpr DWORD kReadyPollMilliseconds = 10;
  bool ready_seen = false;
  for (DWORD waited = 0; waited < kReadyWaitMilliseconds;
       waited += kReadyPollMilliseconds) {
    if (GetFileAttributesA(ready.c_str()) != INVALID_FILE_ATTRIBUTES) {
      ready_seen = true;
      break;
    }
    Sleep(kReadyPollMilliseconds);
  }
  ASSERT_TRUE(ready_seen);

  SetLogDestination(NGLOG_INFO, dest.c_str());
  CaptureTestStderr();
  LOG(INFO) << "message that cannot be written while the file is locked";
  const std::string stderr_output = GetCapturedTestStderr();
  EXPECT_THAT(stderr_output, HasSubstr("Could not create log file '"));

  EXPECT_TRUE(TerminateProcess(process_handle.get(), 0));
  EXPECT_EQ(WaitForSingleObject(process_handle.get(), kReadyWaitMilliseconds),
            WAIT_OBJECT_0);
  constexpr DWORD kLockReleaseWaitMilliseconds = 100;
  Sleep(kLockReleaseWaitMilliseconds);

  std::ifstream input{dest, std::ios::binary};
  const std::string contents{std::istreambuf_iterator<char>{input},
                             std::istreambuf_iterator<char>{}};
  input.close();
  EXPECT_EQ(contents, original_contents);

  LogToStderr();
  DeleteFiles(dest + "*");
#endif
}

TEST(Logging, LogFileIsNotInheritedByChildProcesses) {
#if defined(NGLOG_OS_WINDOWS) && defined(HAVE_SUBPROCESS)
  using namespace std::chrono_literals;
  constexpr auto kChildTimeout = 10s;

  nglog::ScopedFlags<> saver;
  nglog::SetFlag(FLAGS_logtostderr, false);
  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, false);

  const std::string dest = TestTmpDir() + "/logging_test_uninherited_file";
  DeleteFiles(dest + "*");

  SetLogDestination(NGLOG_INFO, dest.c_str());
  LOG(INFO) << "message that opens the log file";
  ASSERT_TRUE(std::ifstream{dest}.good());

  // The child inherits all inheritable handles of this process and stays
  // alive until its stdin is closed.
  char program[] = EMAIL_TEST_HELPER_PATH;
  char* const child_argv[] = {program, nullptr};
  char* const child_envp[] = {nullptr};
  Subprocess<> child;
  ASSERT_TRUE(child.Spawn(child_argv, child_envp));

  // Changing the destination closes the log file in this process. A log file
  // handle inherited by the child would keep the file open.
  const std::string other_dest = dest + ".other";
  SetLogDestination(NGLOG_INFO, other_dest.c_str());
  const int removed = std::remove(dest.c_str());
  const int remove_error = errno;

  child.CloseStdin();
  child.Wait(kChildTimeout);

  EXPECT_EQ(0, removed) << StrError(remove_error);

  LogToStderr();
  DeleteFiles(dest + "*");
#endif
}

TEST(Logging, FileLockFailureReportsWindowsError) {
#ifdef NGLOG_OS_WINDOWS
  auto save = nglog::MakeScopedFlags(
      nglog::MakeScopedFlag(FLAGS_logtostderr, false),
      nglog::MakeScopedFlag(FLAGS_timestamp_in_logfile_name, false));

  const std::string dest = TestTmpDir() + "/logging_test_lock_error";
  const std::string ready = dest + ".ready";
  DeleteFiles(dest + "*");

  std::ofstream output{dest};
  output << "existing logfile contents";
  output.close();

  std::string command = '"' + std::string(g_argv0) +
                        "\" --nglog-hold-log-lock \"" + dest + "\" \"" + ready +
                        '"';
  std::vector<char> command_line(command.begin(), command.end());
  command_line.push_back('\0');

  STARTUPINFOA startup_info = {};
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process_info = {};
  ASSERT_TRUE(CreateProcessA(nullptr, command_line.data(), nullptr, nullptr,
                             FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                             &startup_info, &process_info));
  WindowsHandle process_handle{process_info.hProcess};
  WindowsHandle thread_handle{process_info.hThread};

  constexpr DWORD kReadyWaitMilliseconds = 10'000;
  constexpr DWORD kReadyPollMilliseconds = 10;
  bool ready_seen = false;
  for (DWORD waited = 0; waited < kReadyWaitMilliseconds;
       waited += kReadyPollMilliseconds) {
    if (GetFileAttributesA(ready.c_str()) != INVALID_FILE_ATTRIBUTES) {
      ready_seen = true;
      break;
    }
    Sleep(kReadyPollMilliseconds);
  }
  ASSERT_TRUE(ready_seen);

  SetLogDestination(NGLOG_WARNING, dest.c_str());
  CaptureTestStderr();
  LOG(WARNING) << "message that cannot be written while the file is locked";
  const std::string stderr_output = GetCapturedTestStderr();
  EXPECT_THAT(stderr_output, HasSubstr("Could not create log file '"));
  EXPECT_THAT(stderr_output, HasSubstr(dest));
  std::string expected_error = nglog::tools::TrimTrailingCRLF(
      nglog::tools::FormatWindowsMessage(ERROR_LOCK_VIOLATION));
  ASSERT_FALSE(expected_error.empty());
  EXPECT_THAT(stderr_output, HasSubstr(expected_error));

  EXPECT_TRUE(TerminateProcess(process_handle.get(), 0));
  EXPECT_EQ(WaitForSingleObject(process_handle.get(), kReadyWaitMilliseconds),
            WAIT_OBJECT_0);
  constexpr DWORD kLockReleaseWaitMilliseconds = 100;
  Sleep(kLockReleaseWaitMilliseconds);

  LogToStderr();
  DeleteFiles(dest + "*");
#endif
}

TEST(Logging, Symlink) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, false);

#ifdef NGLOG_OS_WINDOWS
  fprintf(stderr, "==== Test setting Windows log file shortcut\n");
  const string dest = TestTmpDir() + "\\logging_test_shortcut";
  const string sym = TestTmpDir() + "\\symlinkbase";
  const string link_dir = TestTmpDir() + "\\logging_links";
  const string shortcut = sym + ".INFO.lnk";
  const string external_shortcut = link_dir + "\\symlinkbase.INFO.lnk";
  ASSERT_TRUE(CreateDirectoryA(link_dir.c_str(), nullptr) ||
              GetLastError() == ERROR_ALREADY_EXISTS);
  DeleteFiles(dest + "*");
  ASSERT_TRUE(DeleteFileA(shortcut.c_str()) ||
              GetLastError() == ERROR_FILE_NOT_FOUND);
  ASSERT_TRUE(DeleteFileA(external_shortcut.c_str()) ||
              GetLastError() == ERROR_FILE_NOT_FOUND);

  nglog::SetFlag(FLAGS_log_link, link_dir);
  SetLogSymlink(NGLOG_INFO, "symlinkbase");
  SetLogDestination(NGLOG_INFO, dest.c_str());
  LOG(INFO) << "message to new shortcut";
  FlushLogFiles(NGLOG_INFO);

  for (const string& path : {shortcut, external_shortcut}) {
    std::ifstream file(path, std::ios::binary);
    ASSERT_TRUE(file.is_open()) << path;
    unsigned char header[4] = {};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    ASSERT_EQ(file.gcount(), static_cast<std::streamsize>(sizeof(header)));
    EXPECT_EQ(header[0], 0x4c);
    EXPECT_EQ(header[1], 0x00);
    EXPECT_EQ(header[2], 0x00);
    EXPECT_EQ(header[3], 0x00);
  }

  LogToStderr();
  DeleteFiles(dest + "*");
  ASSERT_TRUE(DeleteFileA(shortcut.c_str()) ||
              GetLastError() == ERROR_FILE_NOT_FOUND);
  ASSERT_TRUE(DeleteFileA(external_shortcut.c_str()) ||
              GetLastError() == ERROR_FILE_NOT_FOUND);
  ASSERT_TRUE(RemoveDirectoryA(link_dir.c_str()));
#else
  fprintf(stderr, "==== Test setting log file symlink\n");
  string dest = TestTmpDir() + "/logging_test_symlink";
  string sym = TestTmpDir() + "/symlinkbase";
  DeleteFiles(dest + "*");
  DeleteFiles(sym + "*");

  SetLogSymlink(NGLOG_INFO, "symlinkbase");
  SetLogDestination(NGLOG_INFO, dest.c_str());
  LOG(INFO) << "message to new symlink";
  FlushLogFiles(NGLOG_INFO);
  CheckFile(sym, "message to new symlink");

  // Release file handle for the destination file to unlock the file in Windows.
  LogToStderr();
  DeleteFiles(dest + "*");
  DeleteFiles(sym + "*");
#endif
}

TEST(Logging, Extension) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, false);

  fprintf(stderr, "==== Test setting log file extension\n");
  string dest = TestTmpDir() + "/logging_test_extension";
  DeleteFiles(dest + "*");

  SetLogDestination(NGLOG_INFO, dest.c_str());
  SetLogFilenameExtension("specialextension");
  LOG(INFO) << "message to new extension";
  FlushLogFiles(NGLOG_INFO);
  CheckFile(dest, "message to new extension");

  // Check that file name ends with extension
  vector<string> filenames;
  GetFiles(dest + "*", &filenames);
  CHECK_EQ(filenames.size(), 1UL);
  CHECK(strstr(filenames[0].c_str(), "specialextension") != nullptr);

  // Release file handle for the destination file to unlock the file in Windows.
  LogToStderr();
  DeleteFiles(dest + "*");
}

TEST(Logging, WritesToLongPath) {
#ifdef NGLOG_OS_WINDOWS
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, false);
  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, false);

  nglog::testing_internal::LongPathTestDirectory directory;
  const std::string directory_name =
      "nglog_logging_long_path_" + std::to_string(_getpid());
  ASSERT_TRUE(directory.CreateInTemporaryDirectory(directory_name));
  const std::string path = directory.ChildPath("logging_test");

  SetLogDestination(NGLOG_INFO, path.c_str());
  LOG(INFO) << "message written through a long path";
  FlushLogFiles(NGLOG_INFO);
  LogToStderr();

  struct stat file_status = {};
  ASSERT_EQ(nglog::internal::StatUtf8(path.data(), path.size(), &file_status),
            0);
  EXPECT_GT(file_status.st_size, 0);
  EXPECT_EQ(nglog::internal::UnlinkUtf8(path.data(), path.size()), 0);
#else
  GTEST_SKIP() << "Requires Windows";
#endif
}

TEST(Logging, TruncatesLongPath) {
#if defined(NGLOG_OS_WINDOWS) && defined(HAVE__CHSIZE_S)
  nglog::testing_internal::LongPathTestDirectory directory;
  const std::string directory_name =
      "nglog_truncate_long_path_" + std::to_string(_getpid());
  ASSERT_TRUE(directory.CreateInTemporaryDirectory(directory_name));
  const std::string path = directory.ChildPath("truncate_test");

  const int file_descriptor = nglog::internal::OpenUtf8(
      path.data(), path.size(), O_RDWR | O_CREAT | O_TRUNC | O_BINARY,
      _S_IREAD | _S_IWRITE);
  ASSERT_NE(file_descriptor, -1);
  constexpr char kContents[] = "discardkeep";
  ASSERT_EQ(_write(file_descriptor, kContents, sizeof(kContents) - 1),
            static_cast<int>(sizeof(kContents) - 1));
  ASSERT_EQ(_close(file_descriptor), 0);

  TruncateLogFile(path.c_str(), 10, 4);

  struct stat file_status = {};
  ASSERT_EQ(nglog::internal::StatUtf8(path.data(), path.size(), &file_status),
            0);
  EXPECT_EQ(file_status.st_size, 4);
  EXPECT_EQ(nglog::internal::UnlinkUtf8(path.data(), path.size()), 0);
#else
  GTEST_SKIP() << "Requires Windows with _chsize_s";
#endif
}

TEST(Logging, CleansLongPath) {
#ifdef NGLOG_OS_WINDOWS
  using namespace std::chrono_literals;
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, false);
  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, false);

  nglog::testing_internal::LongPathTestDirectory directory;
  const std::string directory_name =
      "nglog_cleaner_long_path_" + std::to_string(_getpid());
  ASSERT_TRUE(directory.CreateInTemporaryDirectory(directory_name));
  const std::string path = directory.ChildPath("cleanup_test");

  SetLogDestination(NGLOG_INFO, path.c_str());
  LOG(INFO) << "message for long-path cleanup";
  FlushLogFiles(NGLOG_INFO);
  LogToStderr();

  EnableLogCleaner(0h);
  DisableLogCleaner();

  const bool file_exists =
      nglog::internal::AccessUtf8(path.data(), path.size(), 0) == 0;
  EXPECT_FALSE(file_exists);
  if (file_exists) {
    EXPECT_EQ(nglog::internal::UnlinkUtf8(path.data(), path.size()), 0);
  }
#elif defined(HAVE_POSIX_LONG_PATH_SUPPORT)
  using namespace std::chrono_literals;
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, false);
  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, true);

  PosixLongPathTestDirectory directory;
  const std::string directory_name =
      "nglog_cleaner_long_path_" + std::to_string(getpid());
  ASSERT_TRUE(directory.CreateInTemporaryDirectory(directory_name));
  constexpr char kLogBase[] = "cleanup_test";
  const std::string path = directory.ChildPath(kLogBase);

  SetLogDestination(NGLOG_INFO, path.c_str());
  LOG(INFO) << "message for long-path cleanup";
  FlushLogFiles(NGLOG_INFO);
  LogToStderr();

  const std::string stale_path =
      tools::MakeLogFilename(path, "20200101-000000.1", std::string(), true);
  FileDescriptor stale_file{
      internal::OpenPosixPath(stale_path, O_WRONLY | O_CREAT, S_IRUSR)};
  ASSERT_TRUE(stale_file);

  EnableLogCleaner(0h);
  DisableLogCleaner();

  EXPECT_EQ(internal::AccessPosixPath(stale_path, F_OK), -1);
  if (internal::AccessPosixPath(stale_path, F_OK) == 0) {
    EXPECT_EQ(internal::UnlinkPosixPath(stale_path), 0);
  }
#else
  GTEST_SKIP() << "Requires long path support";
#endif
}

#ifdef HAVE_POSIX_LONG_PATH_SUPPORT
TEST(PosixPathOperations, LongPathWithDotComponents) {
  PosixLongPathTestDirectory directory;
  const std::string directory_name =
      "nglog_posix_dot_components_" + std::to_string(getpid());
  ASSERT_TRUE(directory.CreateInCurrentDirectory(directory_name));

  constexpr char kFilename[] = "dot_components_file";
  std::string path = "./" + directory.path();
  std::size_t separator = 0;
  while ((separator = path.find('/', separator + 1)) != std::string::npos) {
    path.insert(separator, "/.");
    separator += 2;
  }
  path += "/./";
  path += kFilename;

  FileDescriptor fd{internal::OpenPosixPath(path, O_CREAT | O_WRONLY | O_TRUNC,
                                            S_IRUSR | S_IWUSR)};
  ASSERT_TRUE(fd) << std::strerror(errno);
  fd.reset();
  EXPECT_EQ(internal::AccessPosixPath(directory.ChildPath(kFilename), F_OK), 0);
  EXPECT_EQ(internal::UnlinkPosixPath(path), 0);
}

TEST(PosixPathOperations, LongPathOfSeparatorsNamesRoot) {
  constexpr long kDefaultPathLimit = 4096;
  const long configured_path_limit = pathconf("/", _PC_PATH_MAX);
  const std::size_t path_limit = static_cast<std::size_t>(
      configured_path_limit > 0 ? configured_path_limit : kDefaultPathLimit);
  const std::string path(path_limit + 1, '/');

  struct stat status;
  ASSERT_EQ(internal::StatPosixPath(path, &status), 0) << std::strerror(errno);
  EXPECT_TRUE(S_ISDIR(status.st_mode));

  struct stat root_status;
  ASSERT_EQ(stat("/", &root_status), 0);
  EXPECT_EQ(status.st_ino, root_status.st_ino);
  EXPECT_EQ(status.st_dev, root_status.st_dev);
}

TEST(PosixPathOperations, SearchOnlyParents) {
  PosixLongPathTestDirectory directory;
  const std::string directory_name =
      "nglog_posix_search_only_" + std::to_string(getpid());
  ASSERT_TRUE(directory.CreateInTemporaryDirectory(directory_name));

  constexpr char kFilename[] = "search_only_file";
  const std::string filename = directory.ChildPath(kFilename);
  FileDescriptor fd{internal::OpenPosixPath(
      filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR)};
  ASSERT_TRUE(fd) << std::strerror(errno);
  fd.reset();
  ASSERT_TRUE(directory.MakeSearchOnly());

  fd.reset(internal::OpenPosixPath(filename, O_RDONLY, 0));
  ASSERT_TRUE(fd) << std::strerror(errno);
}
#endif

struct MyLogger : public base::Logger {
  string data;

  explicit MyLogger(bool* set_on_destruction)
      : set_on_destruction_(set_on_destruction) {}

  ~MyLogger() override { *set_on_destruction_ = true; }

  void Write(bool /* should_flush */,
             const std::chrono::system_clock::time_point& /* timestamp */,
             const char* message, size_t length) override {
    data.append(message, length);
  }

  void Flush() override {}

  uint32 LogSize() override { return static_cast<uint32>(data.length()); }

 private:
  bool* set_on_destruction_;
};

TEST(Logging, Wrapper) {
  // The configured logger is bypassed while FLAGS_logtostderr is true.
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, false);

  fprintf(stderr, "==== Test log wrapper\n");

  bool custom_logger_deleted = false;
  auto my_logger_owner = std::make_unique<MyLogger>(&custom_logger_deleted);
  auto* my_logger = my_logger_owner.get();
  base::Logger* old_logger = base::GetLogger(NGLOG_INFO);
  base::SetLogger(NGLOG_INFO, my_logger_owner.release());
  LOG(INFO) << "Send to wrapped logger";
  CHECK(strstr(my_logger->data.c_str(), "Send to wrapped logger") != nullptr);
  FlushLogFiles(NGLOG_INFO);

  EXPECT_FALSE(custom_logger_deleted);
  base::SetLogger(NGLOG_INFO, old_logger);
  EXPECT_TRUE(custom_logger_deleted);
}

TEST(Logging, Errno) {
  // Plain LOG(), not the shared TestLogging(): that helper's LOG_EVERY_N
  // counters never reset, so sharing it would couple this test's run order
  // to TestLogging(true)'s expected counts elsewhere.
  fprintf(stderr, "==== Test errno preservation\n");

  errno = ENOENT;
  LOG(INFO) << "foo";
  CHECK_EQ(errno, ENOENT);
}

static void TestOneTruncate(const char* path, uint64 limit, uint64 keep,
                            size_t dsize, size_t ksize, size_t expect) {
  FileDescriptor fd{open(path, O_RDWR | O_CREAT | O_TRUNC, 0600)};
  CHECK_ERR(fd);

  const char *discardstr = "DISCARDME!", *keepstr = "KEEPME!";
  const size_t discard_size = strlen(discardstr), keep_size = strlen(keepstr);

  // Fill the file with the requested data; first discard data, then kept data
  size_t written = 0;
  while (written < dsize) {
    size_t bytes = min(dsize - written, discard_size);
    CHECK_ERR(write(fd.get(), discardstr, bytes));
    written += bytes;
  }
  written = 0;
  while (written < ksize) {
    size_t bytes = min(ksize - written, keep_size);
    CHECK_ERR(write(fd.get(), keepstr, bytes));
    written += bytes;
  }

  TruncateLogFile(path, limit, keep);

  // File should now be shorter
  struct stat statbuf;
  CHECK_ERR(fstat(fd.get(), &statbuf));
  CHECK_EQ(static_cast<size_t>(statbuf.st_size), expect);
  CHECK_ERR(lseek(fd.get(), 0, SEEK_SET));

  // File should contain the suffix of the original file
  const size_t buf_size = static_cast<size_t>(statbuf.st_size) + 1;
  std::vector<char> buf(buf_size);
  CHECK_ERR(read(fd.get(), buf.data(), buf_size));

  const char* p = buf.data();
  size_t checked = 0;
  while (checked < expect) {
    size_t bytes = min(expect - checked, keep_size);
    CHECK(!memcmp(p, keepstr, bytes));
    checked += bytes;
  }
}

TEST(Logging, TruncateKeepsTrailingLinesByteForByte) {
#if defined(HAVE_UNISTD_H) || defined(HAVE__CHSIZE_S)
  const string path = TestTmpDir() + "/truncate_lines";
  constexpr char kDiscarded[] = "discarded line\n";
  constexpr char kKept[] = "kept line\n";
  constexpr std::size_t kKeptLength = sizeof(kKept) - 1;
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << kDiscarded << kKept;
    ASSERT_TRUE(output);
  }

  TruncateLogFile(path.c_str(), kKeptLength, kKeptLength);

  std::ifstream input(path, std::ios::binary);
  const std::string contents{std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>()};
  EXPECT_EQ(contents, kKept);
  input.close();
  DeleteFiles(path);
#else
  GTEST_SKIP() << "Log truncation is not supported";
#endif
}

TEST(Logging, Truncate) {
#if defined(HAVE_UNISTD_H) || defined(HAVE__CHSIZE_S)
  fprintf(stderr, "==== Test log truncation\n");
  string path = TestTmpDir() + "/truncatefile";

  // Test on a small file
  TestOneTruncate(path.c_str(), 10, 10, 10, 10, 10);

  // And a big file (multiple blocks to copy)
  TestOneTruncate(path.c_str(), 2U << 20U, 4U << 10U, 3U << 20U, 4U << 10U,
                  4U << 10U);

  // Check edge-case limits
  TestOneTruncate(path.c_str(), 10, 20, 0, 20, 20);
  TestOneTruncate(path.c_str(), 10, 0, 0, 0, 0);
  TestOneTruncate(path.c_str(), 10, 50, 0, 10, 10);
  TestOneTruncate(path.c_str(), 50, 100, 0, 30, 30);

  // MacOSX 10.4 doesn't fail in this case.
  // Windows doesn't have symlink.
  // Emscripten follows symlinks despite O_NOFOLLOW.
  // Let's just ignore this test for these cases.
#  if !defined(NGLOG_OS_MACOSX) && !defined(NGLOG_OS_WINDOWS) && \
      !defined(NGLOG_OS_EMSCRIPTEN)
  // Through a symlink should fail to truncate
  string linkname = path + ".link";
  unlink(linkname.c_str());
  CHECK_ERR(symlink(path.c_str(), linkname.c_str()));
  TestOneTruncate(linkname.c_str(), 10, 10, 0, 30, 30);
#  endif

  // The /proc/self path makes sense only for linux.
#  if defined(NGLOG_OS_LINUX)
  // Through an open fd symlink should work
  int fd;
  CHECK_ERR(fd = open(path.c_str(), O_APPEND | O_WRONLY));
  char fdpath[64];
  std::snprintf(fdpath, sizeof(fdpath), "/proc/self/fd/%d", fd);
  TestOneTruncate(fdpath, 10, 10, 10, 10, 10);
#  endif

#endif
}

#ifdef NGLOG_OS_WINDOWS
TEST(Logging, TruncateStdoutStderr) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_max_log_size, 1U);

  constexpr std::size_t kKeptSize = 1U << 20U;
  constexpr char kPending[] = "pending";
  constexpr char kTail[] = "tail";

  CaptureTestStdout();
  const string message(2U << 20U, 'x');
  ASSERT_EQ(fwrite(message.data(), message.size(), 1, stdout), 1U);
  ASSERT_EQ(fflush(stdout), 0);
  // Buffered output must be part of the kept data.
  ASSERT_GE(fputs(kPending, stdout), 0);

  TruncateStdoutStderr();
  // Output after the truncation must continue at the new end of the file.
  ASSERT_GE(fputs(kTail, stdout), 0);
  ASSERT_EQ(fflush(stdout), 0);
  const string output = GetCapturedTestStdout();

  const string written = message + kPending;
  const string expected = written.substr(written.size() - kKeptSize) + kTail;
  // Avoid printing megabytes of output on failure.
  ASSERT_EQ(output.size(), expected.size());
  EXPECT_EQ(output.find('\0'), string::npos);
  EXPECT_TRUE(output == expected);
}
#endif

TEST(Logging, DropLogMemoryConcurrentWriters) {
#if defined(NGLOG_OS_LINUX) && defined(HAVE_POSIX_FADVISE)
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, false);

  fprintf(stderr,
          "==== Test concurrent writers while drop_log_memory triggers "
          "posix_fadvise\n");
  const std::string dest = TestTmpDir() + "/logging_test_drop_log_memory";
  DeleteFiles(dest + "*");

  // Force every write to flush and re-evaluate the drop_log_memory
  // threshold, so that posix_fadvise() is exercised multiple times while
  // other threads are concurrently appending to the same log file.
  const bool old_drop_log_memory = nglog::GetFlag(FLAGS_drop_log_memory);
  nglog::SetFlag(FLAGS_drop_log_memory, true);
  const int32 old_logbufsecs = nglog::GetFlag(FLAGS_logbufsecs);
  nglog::SetFlag(FLAGS_logbufsecs, 0);

  SetLogDestination(NGLOG_INFO, dest.c_str());

  constexpr int kThreads = 8;
  constexpr int kMessagesPerThread = 400;
  const std::string filler(3000, 'x');

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&filler] {
      for (int i = 0; i < kMessagesPerThread; ++i) {
        LOG(INFO) << filler;
      }
    });
  }
  // The writers repeatedly exercise the path that releases the per-file
  // mutex around posix_fadvise(). Joining verifies that this does not
  // deadlock. The checks below verify that no messages were lost or torn.
  for (auto& thread : threads) {
    thread.join();
  }

  FlushLogFiles(NGLOG_INFO);

  // None of the concurrently written messages should have been lost or
  // corrupted by unlocking mutex_ around posix_fadvise().
  std::vector<std::string> filenames;
  GetFiles(dest + "*", &filenames);
  CHECK_EQ(filenames.size(), 1UL);

  std::ifstream file(filenames[0]);
  CHECK(file.is_open()) << ": could not open " << filenames[0];
  int line_count = 0;
  for (std::string line; std::getline(file, line);) {
    if (line.find(filler) != std::string::npos) {
      ++line_count;
    }
  }
  CHECK_EQ(line_count, kThreads * kMessagesPerThread);

  // Release file handle for the destination file to unlock the file in
  // Windows.
  LogToStderr();
  DeleteFiles(dest + "*");
  nglog::SetFlag(FLAGS_drop_log_memory, old_drop_log_memory);
  nglog::SetFlag(FLAGS_logbufsecs, old_logbufsecs);
#endif
}

TEST(Logging, MessageCountDoesNotWaitForLogger) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, false);
  nglog::SetFlag(FLAGS_logtostdout, false);

  base::Logger* old_logger = base::GetLogger(NGLOG_INFO);
  auto logger_owner = std::make_unique<BlockingLogger>();
  auto* logger = logger_owner.get();
  base::SetLogger(NGLOG_INFO, logger_owner.release());

  std::thread logging_thread([] { LOG(INFO) << "blocked logger"; });
  logger->WaitUntilEntered();

  // Hangs until the CTest timeout if counting waits for the blocked logger.
  static_cast<void>(LogMessage::num_messages(NGLOG_INFO));

  logger->Release();
  logging_thread.join();
  base::SetLogger(NGLOG_INFO, old_logger);
}

TEST(Logging, DefaultFileDispatchDoesNotRelockFile) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, false);
  nglog::SetFlag(FLAGS_logtostdout, false);
  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, false);

  const std::string destination =
      TestTmpDir() + "/logging_test_default_file_dispatch";
  DeleteFiles(destination + "*");
  SetLogDestination(NGLOG_INFO, destination.c_str());

#ifdef NGLOG_ENABLE_LOCK_METRICS
  internal::ResetLockMetrics();
#endif
  LOG(INFO) << "default file dispatch";

#ifdef NGLOG_ENABLE_LOCK_METRICS
  const internal::LockMetrics metrics =
      internal::GetLockMetrics(internal::LockKind::kFile);
  EXPECT_EQ(metrics.acquisitions, 0U);
#endif

  LogToStderr();
  DeleteFiles(destination + "*");
}

struct RecordDeletionLogger : public base::Logger {
  RecordDeletionLogger(bool* set_on_destruction, base::Logger* wrapped_logger)
      : set_on_destruction_(set_on_destruction),
        wrapped_logger_(wrapped_logger) {
    *set_on_destruction_ = false;
  }
  ~RecordDeletionLogger() override { *set_on_destruction_ = true; }
  void Write(bool force_flush,
             const std::chrono::system_clock::time_point& timestamp,
             const char* message, size_t length) override {
    wrapped_logger_->Write(force_flush, timestamp, message, length);
  }
  void Flush() override { wrapped_logger_->Flush(); }
  uint32 LogSize() override { return wrapped_logger_->LogSize(); }

 private:
  bool* set_on_destruction_;
  base::Logger* wrapped_logger_;
};

TEST(Logging, CustomLoggerDeletionOnShutdown) {
  bool custom_logger_deleted = false;
  auto logger = std::make_unique<RecordDeletionLogger>(
      &custom_logger_deleted, base::GetLogger(NGLOG_INFO));
  base::SetLogger(NGLOG_INFO, logger.release());
  EXPECT_TRUE(IsLoggingInitialized());
  ShutdownLogging();
  EXPECT_TRUE(custom_logger_deleted);
  EXPECT_FALSE(IsLoggingInitialized());

  // Re-initialize: other tests assume logging stays initialized and the
  // custom prefix formatter stays installed.
  InitializeLogging(g_argv0);
  InstallPrefixFormatter(&PrefixAttacher, &g_prefix_attacher_data);
  EXPECT_TRUE(IsLoggingInitialized());
}

namespace LogTimes {
// Log a "message" every 10ms, 10 times. These numbers are nice compromise
// between total running time of 100ms and the period of 10ms. The period is
// large enough such that any CPU and OS scheduling variation shouldn't affect
// the results from the ideal case by more than 5% (500us or 0.5ms)
constexpr std::int64_t LOG_PERIOD_NS = 10000000;    // 10ms
constexpr std::int64_t LOG_PERIOD_TOL_NS = 500000;  // 500us

// Set an upper limit for the number of times the stream operator can be
// called. Make sure not to exceed this number of times the stream operator is
// called, since it is also the array size and will be indexed by the stream
// operator.
constexpr size_t MAX_CALLS = 10;
}  // namespace LogTimes

struct LogTimeRecorder {
  LogTimeRecorder() = default;
  size_t m_streamTimes{0};
  std::chrono::steady_clock::time_point m_callTimes[LogTimes::MAX_CALLS];
};
// The stream operator is called by LOG_EVERY_T every time a logging event
// occurs. Make sure to save the times for each call as they will be used later
// to verify the time delta between each call.
std::ostream& operator<<(std::ostream& stream, LogTimeRecorder& t) {
  t.m_callTimes[t.m_streamTimes++] = std::chrono::steady_clock::now();
  return stream;
}
// get elapsed time in nanoseconds
std::int64_t elapsedTime_ns(const std::chrono::steady_clock::time_point& begin,
                            const std::chrono::steady_clock::time_point& end) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>((end - begin))
      .count();
}

TEST(Logging, LogPeriodically) {
  fprintf(stderr, "==== Test log periodically\n");

  LogTimeRecorder timeLogger;

  constexpr double LOG_PERIOD_SEC = LogTimes::LOG_PERIOD_NS * 1e-9;

  while (timeLogger.m_streamTimes < LogTimes::MAX_CALLS) {
    LOG_EVERY_T(INFO, LOG_PERIOD_SEC)
        << timeLogger << "Timed Message #" << timeLogger.m_streamTimes;
  }

  // Calculate time between each call in nanoseconds for higher resolution to
  // minimize error.
  std::int64_t nsBetweenCalls[LogTimes::MAX_CALLS - 1];
  for (size_t i = 1; i < LogTimes::MAX_CALLS; ++i) {
    nsBetweenCalls[i - 1] = elapsedTime_ns(timeLogger.m_callTimes[i - 1],
                                           timeLogger.m_callTimes[i]);
  }

  constexpr std::int64_t kMinimumPeriod =
      LogTimes::LOG_PERIOD_NS - LogTimes::LOG_PERIOD_TOL_NS;
  constexpr std::int64_t kMaximumPeriod =
      LogTimes::LOG_PERIOD_NS + LogTimes::LOG_PERIOD_TOL_NS;
  for (const std::int64_t time_ns : nsBetweenCalls) {
    EXPECT_GE(time_ns, kMinimumPeriod);
    EXPECT_LE(time_ns, kMaximumPeriod);
  }
}

namespace nglog {
inline namespace tools {
// in logging.cc
extern bool SafeFNMatch_(const char* pattern, size_t patt_len, const char* str,
                         size_t str_len);
}  // namespace tools
}  // namespace nglog

static bool WrapSafeFNMatch(string pattern, string str) {
  pattern += "abc";
  str += "defgh";
  return SafeFNMatch_(pattern.data(), pattern.size() - 3, str.data(),
                      str.size() - 5);
}

TEST(SafeFNMatch, logging) {
  CHECK(WrapSafeFNMatch("foo", "foo"));
  CHECK(!WrapSafeFNMatch("foo", "bar"));
  CHECK(!WrapSafeFNMatch("foo", "fo"));
  CHECK(!WrapSafeFNMatch("foo", "foo2"));
  CHECK(WrapSafeFNMatch("bar/foo.ext", "bar/foo.ext"));
  CHECK(WrapSafeFNMatch("*ba*r/fo*o.ext*", "bar/foo.ext"));
  CHECK(!WrapSafeFNMatch("bar/foo.ext", "bar/baz.ext"));
  CHECK(!WrapSafeFNMatch("bar/foo.ext", "bar/foo"));
  CHECK(!WrapSafeFNMatch("bar/foo.ext", "bar/foo.ext.zip"));
  CHECK(WrapSafeFNMatch("ba?/*.ext", "bar/foo.ext"));
  CHECK(WrapSafeFNMatch("ba?/*.ext", "baZ/FOO.ext"));
  CHECK(!WrapSafeFNMatch("ba?/*.ext", "barr/foo.ext"));
  CHECK(!WrapSafeFNMatch("ba?/*.ext", "bar/foo.ext2"));
  CHECK(WrapSafeFNMatch("ba?/*", "bar/foo.ext2"));
  CHECK(WrapSafeFNMatch("ba?/*", "bar/"));
  CHECK(!WrapSafeFNMatch("ba?/?", "bar/"));
  CHECK(!WrapSafeFNMatch("ba?/*", "bar"));
}

// TestWaitingLogSink will save messages here
// No lock: Accessed only by TestLogSinkWriter thread
// and after its demise by its creator.
static vector<string> global_messages;

// helper for TestWaitingLogSink below.
// Thread that does the logic of TestWaitingLogSink
// It's free to use LOG() itself.
class TestLogSinkWriter {
 public:
  TestLogSinkWriter() : t_{&TestLogSinkWriter::Run, this} {}

  // Just buffer it (can't use LOG() here).
  void Buffer(const string& message) {
    mutex_.lock();
    RAW_LOG(INFO, "Buffering");
    messages_.push(message);
    mutex_.unlock();
    RAW_LOG(INFO, "Buffered");
  }

  // Wait for the buffer to clear (can't use LOG() here).
  void Wait() {
    using namespace std::chrono_literals;
    RAW_LOG(INFO, "Waiting");
    mutex_.lock();
    while (!NoWork()) {
      mutex_.unlock();
      std::this_thread::sleep_for(1ms);
      mutex_.lock();
    }
    RAW_LOG(INFO, "Waited");
    mutex_.unlock();
  }

  // Trigger thread exit.
  void Stop() {
    std::lock_guard<std::mutex> l(mutex_);
    should_exit_ = true;
  }

  void Join() { t_.join(); }

 private:
  // helpers ---------------

  // For creating a "Condition".
  bool NoWork() { return messages_.empty(); }
  bool HaveWork() { return !messages_.empty() || should_exit_; }

  // Thread body; CAN use LOG() here!
  void Run() {
    using namespace std::chrono_literals;
    while (true) {
      mutex_.lock();
      while (!HaveWork()) {
        mutex_.unlock();
        std::this_thread::sleep_for(1ms);
        mutex_.lock();
      }
      if (should_exit_ && messages_.empty()) {
        mutex_.unlock();
        break;
      }
      // Give the main thread time to log its message,
      // so that we get a reliable log capture to compare to golden file.
      // Same for the other sleep below.
      std::this_thread::sleep_for(20ms);
      RAW_LOG(INFO, "Sink got a messages");  // only RAW_LOG under mutex_ here
      const string message = messages_.front();
      // Normally this would be some more real/involved logging logic
      // where LOG() usage can't be eliminated,
      // e.g. pushing the message over with an RPC:
      const size_t messages_left = messages_.size() - 1;
      mutex_.unlock();
      std::this_thread::sleep_for(20ms);
      // May not use LOG while holding mutex_, because Buffer()
      // acquires mutex_, and Buffer is called from LOG(),
      // which has its own internal mutex:
      // LOG()->LogToSinks()->TestWaitingLogSink::send()->Buffer()
      LOG(INFO) << "Sink is sending out a message: " << message;
      LOG(INFO) << "Have " << messages_left << " left";
      global_messages.push_back(message);

      // Pop only now: Wait() (and thus WaitTillSent()) treats an empty
      // queue as "message sent", so popping earlier would let it return
      // before the lines above are actually printed.
      std::lock_guard<std::mutex> l(mutex_);
      messages_.pop();
    }
  }

  // data ---------------

  std::mutex mutex_;
  bool should_exit_{false};
  queue<string> messages_;  // messages to be logged
  std::thread t_;
};

// A log sink that exercises WaitTillSent:
// it pushes data to a buffer and wakes up another thread to do the logging
// (that other thread can than use LOG() itself),
class TestWaitingLogSink : public LogSink {
 public:
  TestWaitingLogSink() {
    tid_ = std::this_thread::get_id();  // for thread-specific behavior
    AddLogSink(this);
  }
  ~TestWaitingLogSink() override {
    RemoveLogSink(this);
    writer_.Stop();
    writer_.Join();
  }

  // (re)define LogSink interface

  void send(LogSeverity severity, const char* /* full_filename */,
            const char* base_filename, int line,
            const LogMessageTime& logmsgtime, const char* message,
            size_t message_len) override {
    // Push it to Writer thread if we are the original logging thread.
    // Note: Something like ThreadLocalLogSink is a better choice
    //       to do thread-specific LogSink logic for real.
    if (tid_ == std::this_thread::get_id()) {
      writer_.Buffer(ToString(severity, base_filename, line, logmsgtime,
                              message, message_len));
    }
  }

  void WaitTillSent() override {
    // Wait for Writer thread if we are the original logging thread.
    if (tid_ == std::this_thread::get_id()) writer_.Wait();
  }

 private:
  std::thread::id tid_;
  TestLogSinkWriter writer_;
};

// Check that LogSink::WaitTillSent can be used in the advertised way.
// We also do golden-stderr comparison.
static void TestLogSinkWaitTillSent() {
  // Clear global_messages here to make sure that this test case can be
  // reentered
  global_messages.clear();
  {
    TestWaitingLogSink sink;
    // LogMessage already calls WaitTillSent() on every registered sink
    // after each LOG statement, so no explicit synchronization is needed
    // here between messages.
    LOG(INFO) << "Message 1";
    LOG(ERROR) << "Message 2";
    LOG(WARNING) << "Message 3";
  }
  for (auto& global_message : global_messages) {
    LOG(INFO) << "Sink capture: " << global_message;
  }
  CHECK_EQ(global_messages.size(), 3UL);
}

TEST(Strerror, logging) {
  int errcode = EINTR;
  std::string msg = strerror(errcode);
  const size_t buf_size = msg.size() + 1;
  std::vector<char> buf(buf_size);
  CHECK_EQ(posix_strerror_r(errcode, nullptr, 0), -1);
  buf[0] = 'A';
  CHECK_EQ(posix_strerror_r(errcode, buf.data(), 0), -1);
  CHECK_EQ(buf[0], 'A');
  CHECK_EQ(posix_strerror_r(errcode, nullptr, buf_size), -1);
#if defined(NGLOG_OS_MACOSX) || defined(NGLOG_OS_FREEBSD) || \
    defined(NGLOG_OS_OPENBSD)
  // MacOSX or FreeBSD considers this case is an error since there is
  // no enough space.
  CHECK_EQ(posix_strerror_r(errcode, buf.data(), 1), -1);
#else
  CHECK_EQ(posix_strerror_r(errcode, buf.data(), 1), 0);
#endif
  CHECK_STREQ(buf.data(), "");
  CHECK_EQ(posix_strerror_r(errcode, buf.data(), buf_size), 0);
  CHECK_STREQ(buf.data(), msg.c_str());
  CHECK_EQ(msg, StrError(errcode));
}

// Simple routines to look at the sizes of generated code for LOG(FATAL) and
// CHECK(..) via objdump
/*
static void MyFatal() {
  LOG(FATAL) << "Failed";
}
static void MyCheck(bool a, bool b) {
  CHECK_EQ(a, b);
}
*/

TEST(DVLog, Basic) {
  ScopedMockLog log;

#if defined(NDEBUG)
  // We are expecting that nothing is logged.
  EXPECT_CALL(log, Log(_, _, _)).Times(0);
#else
  EXPECT_CALL(log, Log(NGLOG_INFO, StrEq(__FILE__), StrEq("debug log")));
#endif

  nglog::SetFlag(FLAGS_v, 1);
  DVLOG(1) << "debug log";
}

TEST(DVLog, V0) {
  ScopedMockLog log;

  // We are expecting that nothing is logged.
  EXPECT_CALL(log, Log(_, _, _)).Times(0);

  nglog::SetFlag(FLAGS_v, 0);
  DVLOG(1) << "debug log";
}

TEST(LogAtLevel, Basic) {
  ScopedMockLog log;

  // The function version outputs "logging.h" as a file name.
  EXPECT_CALL(log,
              Log(NGLOG_WARNING, StrNe(__FILE__), StrEq("function version")));
  EXPECT_CALL(log, Log(NGLOG_INFO, StrEq(__FILE__), StrEq("macro version")));

  LogSeverity severity = NGLOG_WARNING;
  LogAtLevel(severity, "function version");

  severity = NGLOG_INFO;
  // We can use the macro version as a C++ stream.
  LOG_AT_LEVEL(severity) << "macro" << ' ' << "version";
}

TEST(Logging, DisabledConditionalLogDoesNotEvaluateArguments) {
  const int saved_min_log_level = nglog::GetFlag(FLAGS_minloglevel);
  nglog::SetFlag(FLAGS_minloglevel, static_cast<std::int32_t>(NGLOG_ERROR));
  testing::MockFunction<int()> disabled_log_argument;
  EXPECT_CALL(disabled_log_argument, Call()).Times(0);

  LOG_IF(INFO, true) << disabled_log_argument.AsStdFunction()();

  nglog::SetFlag(FLAGS_minloglevel, saved_min_log_level);
}

TEST(Logging, FatalIgnoresMinimumLogLevel) {
  const int saved_min_log_level = nglog::GetFlag(FLAGS_minloglevel);
  nglog::SetFlag(FLAGS_minloglevel, NGLOG_FATAL + 1);
  auto const fail_func = InstallFailureFunction(&ThrowFatalLogFailure);
  auto restore = [fail_func, saved_min_log_level] {
    InstallFailureFunction(fail_func);
    nglog::SetFlag(FLAGS_minloglevel, saved_min_log_level);
  };
  ScopedExit<decltype(restore)> restore_state{restore};

  EXPECT_THROW({ LOG(FATAL) << "fatal log"; }, std::logic_error);
  const auto fatal_check = [] { CHECK(false) << "fatal check"; };
  EXPECT_THROW(fatal_check(), std::logic_error);
}

TEST(Logging, DefaultPrefixFormatting) {
  const bool saved_log_to_stderr = nglog::GetFlag(FLAGS_logtostderr);
  const int saved_stderr_threshold = nglog::GetFlag(FLAGS_stderrthreshold);
  const bool saved_log_prefix = nglog::GetFlag(FLAGS_log_prefix);
  const int saved_min_log_level = nglog::GetFlag(FLAGS_minloglevel);

  nglog::SetFlag(FLAGS_logtostderr, true);
  nglog::SetFlag(FLAGS_stderrthreshold, static_cast<std::int32_t>(NGLOG_INFO));
  nglog::SetFlag(FLAGS_log_prefix, true);
  nglog::SetFlag(FLAGS_minloglevel, static_cast<std::int32_t>(NGLOG_INFO));
  InstallPrefixFormatter(nullptr);

  CaptureTestStderr();
  LOG(INFO) << "default prefix formatting";
  const std::string output = GetCapturedTestStderr();

  InstallPrefixFormatter(&PrefixAttacher, &g_prefix_attacher_data);
  nglog::SetFlag(FLAGS_logtostderr, saved_log_to_stderr);
  nglog::SetFlag(FLAGS_stderrthreshold, saved_stderr_threshold);
  nglog::SetFlag(FLAGS_log_prefix, saved_log_prefix);
  nglog::SetFlag(FLAGS_minloglevel, saved_min_log_level);

  EXPECT_THAT(output, HasSubstr("] default prefix formatting\n"));
  EXPECT_THAT(output, testing::Not(HasSubstr("good data")));
}

TEST(TestExitOnDFatal, ToBeOrNotToBe) {
  // Check the default setting...
  EXPECT_TRUE(base::internal::GetExitOnDFatal());

  // Turn off...
  base::internal::SetExitOnDFatal(false);
  EXPECT_FALSE(base::internal::GetExitOnDFatal());

  // We don't die.
  {
    ScopedMockLog log;
    // EXPECT_CALL(log, Log(_, _, _)).Times(AnyNumber());
    //  LOG(DFATAL) has severity FATAL if debugging, but is
    //  downgraded to ERROR if not debugging.
    const LogSeverity severity =
#if defined(NDEBUG)
        NGLOG_ERROR;
#else
        NGLOG_FATAL;
#endif
    EXPECT_CALL(
        log, Log(severity, StrEq(__FILE__), StrEq("This should not be fatal")));
    LOG(DFATAL) << "This should not be fatal";
  }

  // Turn back on...
  base::internal::SetExitOnDFatal(true);
  EXPECT_TRUE(base::internal::GetExitOnDFatal());

#ifdef GTEST_HAS_DEATH_TEST
  // Death comes on little cats' feet.
  EXPECT_DEBUG_DEATH(
      { LOG(DFATAL) << "This should be fatal in debug mode"; },
      "This should be fatal in debug mode");
#endif
}

#ifdef HAVE_STACKTRACE

static void BacktraceAtHelper() {
  LOG(INFO) << "Not me";

  // The vertical spacing of the next 3 lines is significant.
  LOG(INFO) << "Backtrace me";
}
#  ifdef HAVE_SYMBOLIZE
static int kBacktraceAtLine = __LINE__ - 3;  // The line of the LOG(INFO) above
#  endif                                     // HAVE_SYMBOLIZE

TEST(LogBacktraceAt, DoesNotBacktraceWhenDisabled) {
  StrictMock<ScopedMockLog> log;

  nglog::SetFlag(FLAGS_log_backtrace_at, std::string());

  EXPECT_CALL(log, Log(_, _, StrEq("Backtrace me")));
  EXPECT_CALL(log, Log(_, _, StrEq("Not me")));

  BacktraceAtHelper();
}

// Requires HAVE_SYMBOLIZE, not just HAVE_STACKTRACE: it checks that the
// captured backtrace contains actual function names, which needs a working
// symbolizer (e.g. unavailable on MinGW, whose GCC emits debug info the
// Windows dbghelp API can't read).
#  ifdef HAVE_SYMBOLIZE
TEST(LogBacktraceAt, DoesBacktraceAtRightLineWhenEnabled) {
  StrictMock<ScopedMockLog> log;

  char where[100];
  std::snprintf(where, 100, "%s:%d", const_basename(__FILE__),
                kBacktraceAtLine);
  nglog::SetFlag(FLAGS_log_backtrace_at, std::string(where));

  // The LOG at the specified line should include a stacktrace which
  // includes the name of the containing function, followed by the log
  // message. We use HasSubstr()s instead of ContainsRegex() for
  // environments which don't have regexp. The backtrace's actual content
  // is captured rather than matched directly here, since resolving
  // "BacktraceAtHelper"/"main" is not guaranteed on every backend (see
  // below).
  std::string backtrace_message;
  EXPECT_CALL(log, Log(_, _, HasSubstr("stacktrace:")))
      .WillOnce(SaveArg<2>(&backtrace_message));
  // Other LOGs should not include a backtrace.
  EXPECT_CALL(log, Log(_, _, StrEq("Not me")));

  BacktraceAtHelper();

#    ifdef HAVE_ADDR2LINE
  // BacktraceAtHelper() and main() share this translation unit, which a
  // known MinGW/binutils linker quirk (see symbolize.cc) can leave
  // unresolvable via addr2line. Degrade gracefully instead of failing
  // over a toolchain limitation.
  if (backtrace_message.find("BacktraceAtHelper") == std::string::npos) {
    GTEST_SKIP() << "addr2line could not resolve BacktraceAtHelper/main";
  }
#    endif  // HAVE_ADDR2LINE

  EXPECT_THAT(backtrace_message,
              AllOf(HasSubstr("BacktraceAtHelper"), HasSubstr("main"),
                    HasSubstr("Backtrace me")));
}
#  endif  // HAVE_SYMBOLIZE

#endif  // HAVE_STACKTRACE

struct UserDefinedClass {
  bool operator==(const UserDefinedClass&) const { return true; }
};

inline ostream& operator<<(ostream& out, const UserDefinedClass&) {
  out << "OK";
  return out;
}

TEST(UserDefinedClass, logging) {
  UserDefinedClass u;
  vector<string> buf;
  LOG_STRING(INFO, &buf) << u;
  CHECK_EQ(1UL, buf.size());
  CHECK(buf[0].find("OK") != string::npos);

  // We must be able to compile this.
  CHECK_EQ(u, u);
}

TEST(LogMsgTime, gmtoff) {
  /*
   * Unit test for GMT offset API
   * TODO: To properly test this API, we need a platform independent way to set
   * time-zone.
   * */
  nglog::LogMessage log_obj(__FILE__, __LINE__);

  std::chrono::seconds gmtoff = log_obj.time().gmtoffset();
  // GMT offset ranges from UTC-12:00 to UTC+14:00
  using namespace std::chrono_literals;
  constexpr std::chrono::hours utc_min_offset = -12h;
  constexpr std::chrono::hours utc_max_offset = +14h;
  EXPECT_TRUE((gmtoff >= utc_min_offset) && (gmtoff <= utc_max_offset));
}

// Emscripten derives the time zone from the JavaScript host and ignores TZ.
#if !defined(NGLOG_OS_WINDOWS) && !defined(NGLOG_OS_EMSCRIPTEN)
TEST(LogMsgTime, gmtoffSubHour) {
  // The gmtoffset() API is documented to return seconds, so time zones whose
  // offset is not a whole number of hours (e.g. India at UTC+05:30) must be
  // reported without truncating the minutes.
  using namespace std::chrono_literals;
  const char* saved_tz = std::getenv("TZ");
  const std::string saved_tz_value = saved_tz ? saved_tz : std::string{};

  // POSIX TZ format: the offset is the value added to local time to obtain
  // UTC, so "IST-5:30" describes a fixed zone 5 h 30 min east of UTC.
  setenv("TZ", "IST-5:30", 1);
  tzset();

  const std::chrono::seconds gmtoff =
      nglog::LogMessage(__FILE__, __LINE__).time().gmtoffset();

  if (saved_tz) {
    setenv("TZ", saved_tz_value.c_str(), 1);
  } else {
    unsetenv("TZ");
  }
  tzset();

  EXPECT_EQ(gmtoff, 5h + 30min);
}
#endif  // !defined(NGLOG_OS_WINDOWS) && !defined(NGLOG_OS_EMSCRIPTEN)

// Emscripten cannot run a mailer.
#ifndef NGLOG_OS_EMSCRIPTEN
TEST(EmailLogging, ValidAddress) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logmailer, EMAIL_TEST_HELPER_PATH);

  EXPECT_TRUE(
      SendEmail("example@example.com", "Example subject", "Example body"));
}

TEST(EmailLogging, MultipleAddresses) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logmailer, EMAIL_TEST_HELPER_PATH);

  EXPECT_TRUE(SendEmail("example@example.com,foo@bar.com", "Example subject",
                        "Example body"));
}
#endif  // !NGLOG_OS_EMSCRIPTEN

TEST(EmailLogging, InvalidAddress) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logmailer, EMAIL_TEST_HELPER_PATH);

  EXPECT_FALSE(SendEmail("hello world@foo", "Example subject", "Example body"));
}

TEST(EmailLogging, MaliciousAddress) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logmailer, EMAIL_TEST_HELPER_PATH);

  EXPECT_FALSE(
      SendEmail("!/bin/true@example.com", "Example subject", "Example body"));
}

#ifdef HAVE_SUBPROCESS
TEST(EmailLogging, SlowMailerDoesNotBlockOtherLoggingThreads) {
  using namespace std::chrono_literals;
  // Exceeds the CTest timeout, so that a blocked child is reported by CTest
  // instead of failing spuriously on overloaded runners.
  constexpr auto kChildTimeout = 120s;
  constexpr char kExpectedOutput[] = "passed\n";

  const std::string marker_path = TestTmpDir() + "/sleeping-mailer.started";
  const std::string log_path = TestTmpDir() + "/slow-mailer-log";
  DeleteFiles(marker_path);
  DeleteFiles(log_path + "*");

  std::vector<char> program(g_argv0, g_argv0 + std::strlen(g_argv0) + 1);
  std::vector<char> marker(marker_path.begin(), marker_path.end());
  std::vector<char> log(log_path.begin(), log_path.end());
  marker.push_back('\0');
  log.push_back('\0');
  char child_mode[] = "--nglog-slow-mailer";
  char* const child_argv[] = {program.data(), child_mode, marker.data(),
                              log.data(), nullptr};
  char* const child_envp[] = {nullptr};

  Subprocess<> child;
  ASSERT_TRUE(child.Spawn(child_argv, child_envp));
  child.CloseStdin();

  char output[sizeof(kExpectedOutput)] = {};
  std::size_t output_size = 0;
  while (output_size < sizeof(kExpectedOutput) - 1) {
    const std::size_t bytes_read = child.ReadStdout(
        output + output_size, sizeof(output) - output_size, kChildTimeout);
    if (bytes_read == 0) {
      break;
    }
    output_size += bytes_read;
  }
  const auto result = child.Wait(kChildTimeout);
  ASSERT_EQ(nglog::SubprocessWaitResult::kExited, result.status);
  ASSERT_EQ(0, result.exit_code);

  EXPECT_EQ(TrimTrailingCRLF(std::string(output, output_size)),
            TrimTrailingCRLF(kExpectedOutput));
  DeleteFiles(marker_path);
  DeleteFiles(log_path + "*");
}
#endif  // HAVE_SUBPROCESS

TEST(Logging, FatalThrow) {
  auto const fail_func = InstallFailureFunction(&ThrowFatalLogFailure);
  auto restore_fail = [fail_func] { InstallFailureFunction(fail_func); };
  ScopedExit<decltype(restore_fail)> restore{restore_fail};
  EXPECT_THROW({ LOG(FATAL) << "must throw to fail"; }, std::logic_error);
}

// Own suite, not "Logging": gtest schedules suites by first-registered
// case, so sharing "Logging" would run these wherever its first case is
// declared instead of last, where they need to be to capture a clean
// stderr/stdout diff.

TEST(LoggingGoldenFile, Stderr) {
  CaptureTestStderr();

  // re-emit early_stderr
  LogMessage("dummy", LogMessage::kNoLogPrefix, NGLOG_INFO).stream()
      << early_stderr;

  TestLogging(true);
  TestRawLogging();
  TestLoggingLevels();
  TestVLogModule();
  TestLogString();
  TestLogSink();
  TestLogToString();
  TestLogSinkWaitTillSent();
  TestCHECK();
  TestDCHECK();
  TestSTREQ();

  // TODO: The golden test portion of this test is very flakey.
  EXPECT_TRUE(
      MungeAndDiffTestStderr(TestSrcDir() + "/src/logging_unittest.err"));

  nglog::SetFlag(FLAGS_logtostderr, false);
}

TEST(LoggingGoldenFile, Stdout) {
  nglog::SetFlag(FLAGS_logtostdout, true);
  nglog::SetFlag(FLAGS_stderrthreshold, NUM_SEVERITIES);
  CaptureTestStdout();
  TestRawLogging();
  TestLoggingLevels();
  // Same relative position as LoggingGoldenFile.Stderr: VLOG_IS_ON's
  // per-call-site module-level override (set here) only takes effect on
  // GCC; MSVC's VLOG_IS_ON ignores it and always reads FLAGS_v directly.
  // Calling this after TestLoggingLevels() keeps the golden file's expected
  // output free of that override, and thus platform-independent.
  TestVLogModule();
  TestLogString();
  TestLogSink();
  TestLogToString();
  TestLogSinkWaitTillSent();
  TestCHECK();
  TestDCHECK();
  TestSTREQ();
  EXPECT_TRUE(
      MungeAndDiffTestStdout(TestSrcDir() + "/src/logging_unittest.out"));
  nglog::SetFlag(FLAGS_logtostdout, false);
}

#if defined(__GNUC__)
TEST(VLOG, ConcurrentLevelUpdates) {
  constexpr int kInitialLevel = 0;
  constexpr int kUpdateCount = 100000;
  constexpr int kVlogTestLevel = 1;
  constexpr int kVlogLevelPeriod = 2;
  constexpr int kYieldPeriod = 64;
  SetVLOGLevel("logging_unittest", kInitialLevel);
  static_cast<void>(VLOG_IS_ON(kVlogTestLevel));
  std::atomic<bool> stop{false};
  std::condition_variable reader_started_condition;
  std::mutex reader_started_mutex;
  bool reader_started = false;
  std::thread reader([&stop, &reader_started_condition, &reader_started_mutex,
                      &reader_started] {
    {
      std::lock_guard<std::mutex> lock(reader_started_mutex);
      reader_started = true;
    }
    reader_started_condition.notify_one();
    while (!stop.load(std::memory_order_relaxed)) {
      static_cast<void>(VLOG_IS_ON(kVlogTestLevel));
    }
  });

  {
    std::unique_lock<std::mutex> lock(reader_started_mutex);
    reader_started_condition.wait(lock,
                                  [&reader_started] { return reader_started; });
  }

  for (int i = 0; i < kUpdateCount; ++i) {
    SetVLOGLevel("logging_unittest", i % kVlogLevelPeriod);
    if (i % kYieldPeriod == 0) {
      std::this_thread::yield();
    }
  }

  stop.store(true, std::memory_order_relaxed);
  reader.join();
  SetVLOGLevel("logging_unittest", kInitialLevel);
}
#endif

namespace {

template <int CallSiteId>
struct OccasionalCallSite {
  static void Run(int iters, int n, int first_n) {
    for (int j = 0; j < iters; ++j) {
      LOG_EVERY_N(INFO, n) << "Multithreaded every n: " << COUNTER;
      LOG_FIRST_N(WARNING, first_n) << "Multithreaded first n: " << COUNTER;
      LOG_IF_EVERY_N(INFO, true, n) << "Multithreaded if every n: " << COUNTER;
      PLOG_EVERY_N(INFO, n) << "Multithreaded plog every n: " << COUNTER;
    }
  }
};

template <int... Is>
void RunAllOccasionalCallsitesHelper(std::integer_sequence<int, Is...>,
                                     int iters, int n, int first_n) {
  int dummy[] = {0, (OccasionalCallSite<Is>::Run(iters, n, first_n), 0)...};
  (void)dummy;
}

template <int NumCallsites>
void RunAllOccasionalCallsites(int iters, int n, int first_n) {
  RunAllOccasionalCallsitesHelper(
      std::make_integer_sequence<int, NumCallsites>{}, iters, n, first_n);
}

}  // namespace

TEST(Logging, MultithreadedOccasionalLogging) {
  constexpr int kNumThreads = 8;
  constexpr int kItersPerThread = 50;
  constexpr int kN = 10;
  constexpr int kFirstN = 5;
  constexpr int kNumCallsites = 8;

  static std::atomic<int> test_run_counter{0};
  int const run_number =
      test_run_counter.fetch_add(1, std::memory_order_relaxed);

  int64 const base_num_infos = LogMessage::num_messages(NGLOG_INFO);
  int64 const base_num_warnings = LogMessage::num_messages(NGLOG_WARNING);

  std::atomic<int> ready{0};
  std::atomic<bool> start_barrier{false};

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&ready, &start_barrier]() {
      ready.fetch_add(1, std::memory_order_relaxed);
      while (!start_barrier.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      RunAllOccasionalCallsites<kNumCallsites>(kItersPerThread, kN, kFirstN);
    });
  }

  while (ready.load(std::memory_order_relaxed) < kNumThreads) {
    std::this_thread::yield();
  }
  start_barrier.store(true, std::memory_order_release);

  for (auto& t : threads) {
    t.join();
  }

  constexpr int kTotalCallsPerCallsite = kNumThreads * kItersPerThread;
  constexpr int kExpectedEveryNPerCallsite = kTotalCallsPerCallsite / kN;
  // LOG_EVERY_N, LOG_IF_EVERY_N, and PLOG_EVERY_N all log to NGLOG_INFO.
  constexpr int kExpectedInfos =
      kNumCallsites * (kExpectedEveryNPerCallsite * 3);
  // On the first run, LOG_FIRST_N fires up to its threshold across callsites.
  // On subsequent runs, all callsites have already reached their limit.
  int const expected_warnings =
      (run_number == 0) ? (kNumCallsites * kFirstN) : 0;

  EXPECT_EQ(LogMessage::num_messages(NGLOG_INFO) - base_num_infos,
            kExpectedInfos);
  EXPECT_EQ(LogMessage::num_messages(NGLOG_WARNING) - base_num_warnings,
            expected_warnings);
}

TEST(Logging, OccasionalLoggingInSwitchAndGoto) {
  int const x = 1;
  switch (x) {
    case 1:
      LOG_EVERY_N(INFO, 1) << "switch test every n";
      LOG_FIRST_N(INFO, 1) << "switch test first n";
      LOG_IF_EVERY_N(INFO, true, 1) << "switch test if every n";
      PLOG_EVERY_N(INFO, 1) << "switch test plog every n";
      break;
    case 2:
      break;
    default:
      break;
  }

  goto test_label;
  LOG_EVERY_N(INFO, 1) << "unreachable";
test_label:;
}

TEST(Logging, OccasionalLoggingNonPositiveN) {
  LOG_EVERY_N(INFO, 0) << "Should not divide by zero";
  LOG_EVERY_N(INFO, -1) << "Should not divide by zero";
  LOG_IF_EVERY_N(INFO, true, 0) << "Should not divide by zero";
  LOG_IF_EVERY_N(INFO, true, -5) << "Should not divide by zero";
  PLOG_EVERY_N(INFO, 0) << "Should not divide by zero";
  PLOG_EVERY_N(INFO, -1) << "Should not divide by zero";
  LOG_FIRST_N(INFO, 0) << "Should not log or crash";
  LOG_FIRST_N(INFO, -1) << "Should not log or crash";
}

TEST(Logging, LogIfEveryNCounterSemantics) {
  ScopedMockLog log;
  EXPECT_CALL(log, Log(NGLOG_INFO, _, StrEq("Condition counter: 1")));
  EXPECT_CALL(log, Log(NGLOG_INFO, _, StrEq("Condition counter: 2")));
  EXPECT_CALL(log, Log(NGLOG_INFO, _, StrEq("Condition counter: 3")));

  for (int i = 0; i < 6; ++i) {
    bool const cond = (i % 2 == 0);
    LOG_IF_EVERY_N(INFO, cond, 1) << "Condition counter: " << COUNTER;
  }
}
