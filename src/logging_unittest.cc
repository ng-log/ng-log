// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Ray Sidney

#include <fcntl.h>
#include <sys/types.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <memory>
#include <mutex>
#include <queue>
#include <ratio>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
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
#  include <utime.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#  include <sys/wait.h>
#endif
#ifdef NGLOG_OS_WINDOWS
#  include <windows.h>

#  include <cwchar>
#  include <limits>

#  include "internal/utf8.h"
#  include "long_path_test.h"
#  include "windows/port.h"
#endif

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#ifdef NGLOG_ENABLE_LOCK_METRICS
#  include "internal/lock_metrics.h"
#endif
#include "internal/log_message_access.h"
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
#include "vlog_module_test_helper.h"

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
using nglog::testing_internal::IsVerboseInTestModule;
using nglog::testing_internal::kVlogTestModule;
using testing::_;
using testing::AllOf;
using testing::AnyOf;
using testing::Contains;
using testing::Each;
using testing::ElementsAre;
using testing::ElementsAreArray;
using testing::EndsWith;
using testing::Ge;
using testing::HasSubstr;
using testing::InitGoogleMock;
using testing::IsEmpty;
using testing::Le;
using testing::MatchesRegex;
using testing::Not;
using testing::ResultOf;
using testing::SaveArg;
using testing::SizeIs;
using testing::StartsWith;
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

//! Makes fatal log messages throw std::logic_error while the object lives.
class ScopedThrowOnFatal final {
 public:
  ScopedThrowOnFatal()
      : previous_{InstallFailureFunction(&ThrowFatalLogFailure)} {}
  ScopedThrowOnFatal(const ScopedThrowOnFatal&) = delete;
  ScopedThrowOnFatal& operator=(const ScopedThrowOnFatal&) = delete;
  ~ScopedThrowOnFatal() { InstallFailureFunction(previous_); }

 private:
  logging_fail_func_t previous_;
};

//! Overrides whether LOG(DFATAL) is fatal while the object lives.
class ScopedExitOnDFatal final {
 public:
  explicit ScopedExitOnDFatal(bool value)
      : previous_{base::internal::GetExitOnDFatal()} {
    base::internal::SetExitOnDFatal(value);
  }
  ScopedExitOnDFatal(const ScopedExitOnDFatal&) = delete;
  ScopedExitOnDFatal& operator=(const ScopedExitOnDFatal&) = delete;
  ~ScopedExitOnDFatal() { base::internal::SetExitOnDFatal(previous_); }

 private:
  bool previous_;
};

//! Output that main() captures before logging is initialized.
std::string early_stderr;

//! Pointer value that TestRawLogging() formats using the %p conversion.
constexpr std::uintptr_t kRawLogPointerValue = 0x12345678;

// argv[0] and the prefix formatter context, kept so
// Logging.CustomLoggerDeletionOnShutdown can restore both after its own
// ShutdownLogging() call.
const char* g_argv0 = nullptr;
std::string g_prefix_attacher_data;

// Reinstalls the prefix formatter that main() installs for the whole run.
void RestoreTestPrefixFormatter() {
  InstallPrefixFormatter(&PrefixAttacher, &g_prefix_attacher_data);
}

std::string ReadFileContents(const std::string& path) {
  std::ifstream input{path, std::ios::binary};
  EXPECT_TRUE(input.is_open()) << "could not open " << path;
  return std::string{std::istreambuf_iterator<char>{input},
                     std::istreambuf_iterator<char>{}};
}

//! Splits text into lines without their line terminators.
std::vector<std::string> SplitLines(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream input{text};
  std::string line;
  while (std::getline(input, line)) {
    lines.push_back(line);
  }
  return lines;
}

//! Text that separates the prefix of a log line from its message.
constexpr char kPrefixEnd[] = "] ";
constexpr std::size_t kPrefixEndLength = sizeof(kPrefixEnd) - 1;
//! Base name of this file as the prefix of a log line names it.
constexpr char kSourceBasename[] = "logging_unittest.cc";
//! Regular expression that matches kSourceBasename.
constexpr char kSourceBasenamePattern[] = "logging_unittest\\.cc";

using LineMatcher = testing::Matcher<const std::string&>;

/**
 * Matches a log line whose prefix starts with the given severity character
 * and names this file, and whose message matches `message`. The date, time,
 * thread identifier and line number in the prefix differ between runs and are
 * therefore ignored.
 */
MATCHER_P2(LogLine, severity, message,
           std::string{"is a log line of severity "} + severity +
               " whose message " +
               testing::DescribeMatcher<const std::string&>(message)) {
  if (arg.empty() || arg.front() != severity) {
    *result_listener << "whose severity is not " << severity;
    return false;
  }
  const std::string::size_type prefix_end = arg.find(kPrefixEnd);
  if (prefix_end == std::string::npos) {
    *result_listener << "which lacks the end of a log prefix";
    return false;
  }
  const std::string file = std::string{" "} + kSourceBasename + ":";
  if (arg.rfind(file, prefix_end) == std::string::npos) {
    *result_listener << "whose prefix does not name " << kSourceBasename;
    return false;
  }
  return testing::ExplainMatchResult(LineMatcher{message},
                                     arg.substr(prefix_end + kPrefixEndLength),
                                     result_listener);
}

/**
 * Matches text that starts with `leading` and continues with a match of
 * `rest`.
 */
MATCHER_P2(StartsWithThen, leading, rest,
           std::string{"starts with \""} + leading +
               "\" followed by text that " +
               testing::DescribeMatcher<const std::string&>(rest)) {
  const std::string expected_start{leading};
  if (arg.compare(0, expected_start.size(), expected_start) != 0) {
    *result_listener << "which does not start with \"" << expected_start << '"';
    return false;
  }
  return testing::ExplainMatchResult(
      LineMatcher{rest}, arg.substr(expected_start.size()), result_listener);
}

//! Severity character and message of a log line that a test expects.
struct ExpectedLine {
  char severity;
  const char* message;
};

//! Matches the lines in the given order.
std::vector<LineMatcher> LogLines(const std::vector<ExpectedLine>& lines) {
  std::vector<LineMatcher> matchers;
  matchers.reserve(lines.size());
  for (const ExpectedLine& line : lines) {
    matchers.emplace_back(LogLine(line.severity, line.message));
  }
  return matchers;
}

//! Formats a pointer the way RAW_LOG formats the %p conversion.
std::string FormatPointer(const void* pointer) {
  constexpr std::size_t kBufferSize = 64;
  char buffer[kBufferSize];
  std::snprintf(buffer, sizeof(buffer), "%p", pointer);
  return buffer;
}

#if defined(GTEST_USES_POSIX_RE) && GTEST_USES_POSIX_RE
// POSIX regular expressions lack the shorthand for digits.
constexpr char kDigitPattern[] = "[0-9]";
#else
constexpr char kDigitPattern[] = "\\d";
#endif

//! Regular expression that matches exactly `count` decimal digits.
std::string DigitsPattern(std::size_t count) {
  std::string pattern;
  for (std::size_t i = 0; i < count; ++i) {
    pattern += kDigitPattern;
  }
  return pattern;
}

//! Identifier of the calling thread as a stream prints it.
std::string CurrentThreadId() {
  std::ostringstream id;
  id << std::this_thread::get_id();
  return id.str();
}

/**
 * Returns a regular expression that matches the default prefix
 * "<severity><yyyymmdd> <hh:mm:ss.uuuuuu> <thread id> <file>:<line>] " of a
 * message that the calling thread logs at the given line of this file.
 */
std::string DefaultPrefixPattern(char severity, int line) {
  constexpr std::size_t kDateDigits = 8;
  constexpr std::size_t kTimeFieldDigits = 2;
  constexpr std::size_t kMicrosecondDigits = 6;
  return std::string(1, severity) + DigitsPattern(kDateDigits) + ' ' +
         DigitsPattern(kTimeFieldDigits) + ':' +
         DigitsPattern(kTimeFieldDigits) + ':' +
         DigitsPattern(kTimeFieldDigits) + "\\." +
         DigitsPattern(kMicrosecondDigits) + " +" + CurrentThreadId() + ' ' +
         kSourceBasenamePattern + ':' + std::to_string(line) + "\\] ";
}

/**
 * Makes log messages reach only the given standard stream and resets the
 * other flags that change which messages are written or how they look.
 */
void RouteLogMessagesTo(StandardStream stream) {
  const bool to_stdout = stream == StandardStream::kStdout;
  nglog::SetFlag(FLAGS_logtostdout, to_stdout);
  nglog::SetFlag(FLAGS_logtostderr, !to_stdout);
  nglog::SetFlag(FLAGS_stderrthreshold,
                 to_stdout ? static_cast<std::int32_t>(NUM_SEVERITIES)
                           : static_cast<std::int32_t>(NGLOG_ERROR));
  nglog::SetFlag(FLAGS_alsologtostderr, false);
  nglog::SetFlag(FLAGS_colorlogtostderr, false);
  nglog::SetFlag(FLAGS_colorlogtostdout, false);
  nglog::SetFlag(FLAGS_minloglevel, static_cast<std::int32_t>(NGLOG_INFO));
  nglog::SetFlag(FLAGS_v, 0);
  nglog::SetFlag(FLAGS_log_prefix, true);
  nglog::SetFlag(FLAGS_log_year_in_prefix, true);
  nglog::SetFlag(FLAGS_log_backtrace_at, std::string{});
}

#ifdef NGLOG_OS_WINDOWS
// Bounds waiting for a child process. The tests do not measure time, so the
// bound only needs to end a hang. It stays below the CTest timeout so that a
// hang is reported by the test itself.
constexpr DWORD kChildProcessTimeoutMilliseconds = 20'000;

// Creates a file at `ready_path` to signal the parent and keeps the process
// alive until the parent terminates it. The process exits on its own only
// after the parent stopped waiting for it.
void SignalReadyAndHold(const char* ready_path) {
  std::ofstream ready_file{ready_path};
  ready_file << "ready";
  ready_file.close();
  Sleep(2 * kChildProcessTimeoutMilliseconds);
}

OVERLAPPED MakeLogFileLockRegion() {
  OVERLAPPED overlapped = {};
  overlapped.Offset = static_cast<DWORD>(kLogFileLockOffset);
  overlapped.OffsetHigh =
      static_cast<DWORD>(kLogFileLockOffset >> kWindowsDwordBits);
  return overlapped;
}

bool TryLockLogFile(HANDLE file, OVERLAPPED* region) {
  return LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                    0, kLogFileLockLength, 0, region) != FALSE;
}

/**
 * Runs this test binary with the given command line arguments appended.
 *
 * @return The handle of the child process, or an empty handle if the process
 * could not be created.
 */
WindowsHandle SpawnSelf(const std::string& arguments) {
  const std::string command = '"' + std::string(g_argv0) + "\" " + arguments;
  std::vector<char> command_line(command.begin(), command.end());
  command_line.push_back('\0');

  STARTUPINFOA startup_info = {};
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process_info = {};
  if (!CreateProcessA(nullptr, command_line.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &startup_info,
                      &process_info)) {
    return WindowsHandle{};
  }
  const WindowsHandle thread_handle{process_info.hThread};
  return WindowsHandle{process_info.hProcess};
}

//! Polls `condition` until it holds or the child process timeout ends.
template <class Condition>
bool PollUntil(Condition condition) {
  constexpr DWORD kPollMilliseconds = 10;
  const ULONGLONG deadline =
      GetTickCount64() + kChildProcessTimeoutMilliseconds;
  while (!condition()) {
    if (GetTickCount64() >= deadline) {
      return false;
    }
    Sleep(kPollMilliseconds);
  }
  return true;
}

bool WaitForFile(const std::string& path) {
  return PollUntil([&path] {
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
  });
}

/**
 * Waits until no process holds the log file lock of the file at `path`. The
 * system releases the locks of a terminated process asynchronously.
 */
bool WaitForLogFileUnlock(const std::string& path) {
  WindowsHandle file_handle{
      CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
  if (file_handle.get() == INVALID_HANDLE_VALUE) {
    return false;
  }
  OVERLAPPED region = MakeLogFileLockRegion();
  if (!PollUntil([&file_handle, &region] {
        return TryLockLogFile(file_handle.get(), &region);
      })) {
    return false;
  }
  return UnlockFileEx(file_handle.get(), 0, kLogFileLockLength, 0, &region) !=
         FALSE;
}
#endif

#ifdef HAVE_SUBPROCESS
bool MailerMarkerExists(const std::string& marker_path) {
  std::ifstream marker{marker_path};
  return marker.good();
}

int RunSlowMailerScenario(const char* program, const char* marker_path,
                          const char* log_path) {
  using namespace std::chrono_literals;
  constexpr auto kPollInterval = 10ms;

  if (!SetEnvironmentValue("NGLOG_TEST_MAILER_MARKER", marker_path)) {
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
    if (file_handle.get() == INVALID_HANDLE_VALUE) {
      return EXIT_FAILURE;
    }

    OVERLAPPED region = MakeLogFileLockRegion();
    if (!TryLockLogFile(file_handle.get(), &region)) {
      return EXIT_FAILURE;
    }

    SignalReadyAndHold(argv[3]);
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
      SignalReadyAndHold(argv[3]);
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
  {
    ScopedStreamCapture capture{StandardStream::kStderr};
    LogWithLevels(nglog::GetFlag(FLAGS_v),
                  nglog::GetFlag(FLAGS_stderrthreshold),
                  nglog::GetFlag(FLAGS_logtostderr),
                  nglog::GetFlag(FLAGS_alsologtostderr));
    LogWithLevels(0, 0, false, false);  // simulate "before global c-tors"
    early_stderr = capture.Stop();
  }

  EXPECT_FALSE(IsLoggingInitialized());

  // Install a custom prefix formatter that produces the default layout. Tests
  // can therefore check the same prefix with either formatter.
  g_prefix_attacher_data = "good data";
  InitializeTestLogging(argv[0]);
  RestoreTestPrefixFormatter();

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
  std::int64_t base_num_infos = LogMessage::num_messages(NGLOG_INFO);
  std::int64_t base_num_warning = LogMessage::num_messages(NGLOG_WARNING);
  std::int64_t base_num_errors = LogMessage::num_messages(NGLOG_ERROR);

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
    EXPECT_EQ(LogMessage::num_messages(NGLOG_INFO), base_num_infos + 15);
    EXPECT_EQ(LogMessage::num_messages(NGLOG_WARNING), base_num_warning + 3);
    // ERROR is a range, not an exact count: its EVERY_N periods (3, 4, 2)
    // don't evenly divide their iteration counts, so how many fire depends
    // on the counters' phase, which carries over from any earlier call to
    // this function elsewhere in the process.
    EXPECT_THAT(LogMessage::num_messages(NGLOG_ERROR),
                AllOf(Ge(base_num_errors + 14), Le(base_num_errors + 17)));
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
  void* p = reinterpret_cast<void*>(kRawLogPointerValue);
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

#ifdef GTEST_HAS_DEATH_TEST
// Each death symbolizes a stack trace, which takes long with some backends,
// so every function has a test of its own.
namespace {

const auto kPastLastSeverity = static_cast<LogSeverity>(NUM_SEVERITIES);
constexpr char kPastLastMessage[] = "Check failed: .*NUM_SEVERITIES";

}  // namespace

TEST(DeathSeverityRange, SetLogDestinationRejectsSeveritiesOutOfRange) {
  EXPECT_DEATH(SetLogDestination(kPastLastSeverity, "unused"),
               kPastLastMessage);
  EXPECT_DEATH(SetLogDestination(static_cast<LogSeverity>(-1), "unused"),
               "Check failed: .*>= 0");
}

TEST(DeathSeverityRange, GetLoggerRejectsSeveritiesOutOfRange) {
  EXPECT_DEATH(base::GetLogger(kPastLastSeverity), kPastLastMessage);
}

TEST(DeathSeverityRange, SetLoggerRejectsSeveritiesOutOfRange) {
  EXPECT_DEATH(base::SetLogger(kPastLastSeverity, nullptr), kPastLastMessage);
}

TEST(DeathSeverityRange, GetLogSeverityNameRejectsSeveritiesOutOfRange) {
  EXPECT_DEATH(GetLogSeverityName(kPastLastSeverity), kPastLastMessage);
}

TEST(DeathSeverityRange, NumMessagesRejectsSeveritiesOutOfRange) {
  EXPECT_DEATH(LogMessage::num_messages(NUM_SEVERITIES), kPastLastMessage);
}

// The default prefix of messages to stderr formats the severity name before
// any log file is selected, so the check must happen when the message is
// created.
TEST(DeathSeverityRange, LogMessageRejectsSeveritiesOutOfRange) {
  EXPECT_DEATH(
      {
        nglog::SetFlag(FLAGS_logtostderr, true);
        InstallPrefixFormatter(nullptr);
        LogMessage(__FILE__, __LINE__, kPastLastSeverity).stream() << "unused";
      },
      kPastLastMessage);
}

TEST(DeathSeverityRange, LogSinkToStringRejectsSeveritiesOutOfRange) {
  const LogMessageTime time;
  EXPECT_DEATH(LogSink::ToString(kPastLastSeverity, __FILE__, __LINE__, time,
                                 "unused", sizeof("unused") - 1),
               kPastLastMessage);
}

TEST(DeathSeverityRange, LogAtLevelRejectsSeveritiesOutOfRange) {
  EXPECT_DEATH(
      {
        nglog::SetFlag(FLAGS_logtostderr, true);
        InstallPrefixFormatter(nullptr);
        LogAtLevel(static_cast<LogSeverity>(-1), "unused");
      },
      "Check failed: .*>= 0");
}

TEST(DeathRawCHECK, logging) {
  ASSERT_DEATH(RAW_CHECK(false, "failure 1"),
               "RAW: Check false failed: failure 1");
  ASSERT_DEBUG_DEATH(RAW_DCHECK(1 == 2, "failure 2"),
                     "RAW: Check 1 == 2 failed: failure 2");
}
#endif

namespace {

constexpr wchar_t kUnpairedSurrogate = L'\xDC00';
// Length of the UTF-8 encoding of L"wide".
constexpr size_t kWideEncodingLength = 4;

// Number of bytes the stream of the message can still hold. The stream keeps
// room for a newline and a null character.
size_t RemainingCapacity(LogMessage& message) {
  constexpr size_t kReservedLength = 2;
  return LogMessage::kMaxLogMessageLen - kReservedLength -
         static_cast<LogMessage::LogStream&>(message.stream()).pcount();
}

}  // namespace

TEST(LogMessageWideText, RecordsItsEncoding) {
  const std::wstring wide{L'a', kUnpairedSurrogate, L'b'};
  vector<string> texts;
  {
    LogMessage message(__FILE__, __LINE__, NGLOG_INFO, &texts);
    message.stream() << "prefix ";
    nglog::internal::LogMessageAccess::AppendWide(message, wide.data(),
                                                  wide.size());
    message.stream() << " suffix";

    const nglog::internal::WideSpan span =
        nglog::internal::LogMessageAccess::WideSpanOf(message);
    EXPECT_EQ(span.text, wide.data());
    EXPECT_EQ(span.length, wide.size());
    ASSERT_NE(span.begin, nullptr);
    EXPECT_EQ(string(span.begin, span.end),
              "a\xEF\xBF\xBD"
              "b");
    EXPECT_EQ(string(span.begin - strlen("prefix "), span.begin), "prefix ");
    EXPECT_EQ(string(span.end, span.end + strlen(" suffix")), " suffix");
  }
  EXPECT_THAT(texts, ElementsAre(HasSubstr("prefix a\xEF\xBF\xBD"
                                           "b suffix")));
}

TEST(LogMessageWideText, IsRecordedWhenItsEncodingFits) {
  const std::wstring wide = L"wide";
  vector<string> texts;
  LogMessage message(__FILE__, __LINE__, NGLOG_INFO, &texts);
  message.stream() << string(RemainingCapacity(message) - kWideEncodingLength,
                             'x');
  nglog::internal::LogMessageAccess::AppendWide(message, wide.data(),
                                                wide.size());

  EXPECT_EQ(nglog::internal::LogMessageAccess::WideSpanOf(message).text,
            wide.data());
}

TEST(LogMessageWideText, IsNotRecordedWhenItsEncodingIsTruncated) {
  const std::wstring wide = L"wide";
  vector<string> texts;
  LogMessage message(__FILE__, __LINE__, NGLOG_INFO, &texts);
  message.stream() << string(
      RemainingCapacity(message) - kWideEncodingLength + 1, 'x');
  nglog::internal::LogMessageAccess::AppendWide(message, wide.data(),
                                                wide.size());

  EXPECT_EQ(nglog::internal::LogMessageAccess::WideSpanOf(message).text,
            nullptr);
}

TEST(LogMessageWideText, IsForgottenByLaterMessages) {
  const std::wstring wide = L"wide";
  vector<string> texts;
  {
    LogMessage first(__FILE__, __LINE__, NGLOG_INFO, &texts);
    nglog::internal::LogMessageAccess::AppendWide(first, wide.data(),
                                                  wide.size());
    ASSERT_EQ(nglog::internal::LogMessageAccess::WideSpanOf(first).text,
              wide.data());
  }
  LogMessage second(__FILE__, __LINE__, NGLOG_INFO, &texts);

  EXPECT_EQ(nglog::internal::LogMessageAccess::WideSpanOf(second).text,
            nullptr);
}

void TestLogString() {
  vector<string> errors;
  vector<string>* no_errors = nullptr;

  LOG_STRING(INFO, &errors) << "LOG_STRING: "
                            << "collected info";
  LOG_STRING(WARNING, &errors) << "LOG_STRING: "
                               << "collected warning";
  LOG_STRING(ERROR, &errors) << "LOG_STRING: "
                             << "collected error";

  LOG_STRING(INFO, no_errors) << "LOG_STRING: "
                              << "reported info";
  LOG_STRING(WARNING, no_errors) << "LOG_STRING: "
                                 << "reported warning";
  LOG_STRING(ERROR, nullptr) << "LOG_STRING: "
                             << "reported error";

  for (auto& error : errors) {
    LOG(INFO) << "Captured by LOG_STRING:  " << error;
  }
}

void TestLogToString() {
  string error;
  string* no_error = nullptr;

  LOG_TO_STRING(INFO, &error) << "LOG_TO_STRING: "
                              << "collected info";
  LOG(INFO) << "Captured by LOG_TO_STRING:  " << error;
  LOG_TO_STRING(WARNING, &error) << "LOG_TO_STRING: "
                                 << "collected warning";
  LOG(INFO) << "Captured by LOG_TO_STRING:  " << error;
  LOG_TO_STRING(ERROR, &error) << "LOG_TO_STRING: "
                               << "collected error";
  LOG(INFO) << "Captured by LOG_TO_STRING:  " << error;

  LOG_TO_STRING(INFO, no_error) << "LOG_TO_STRING: "
                                << "reported info";
  LOG_TO_STRING(WARNING, no_error) << "LOG_TO_STRING: "
                                   << "reported warning";
  LOG_TO_STRING(ERROR, nullptr) << "LOG_TO_STRING: "
                                << "reported error";
}

class TestLogSinkImpl : public LogSink {
 public:
  vector<string> errors;
  void send(LogSeverity severity, const char* /* full_filename */,
            const char* base_filename, int line,
            const LogMessageTime& logmsgtime, const char* message,
            std::size_t message_len) override {
    errors.push_back(ToString(severity, base_filename, line, logmsgtime,
                              message, message_len));
  }
};

class BlockingLogger : public base::Logger {
 public:
  void Write(bool /* force_flush */,
             const std::chrono::system_clock::time_point&,
             const char* /* message */, std::size_t /* length */) override {
    std::unique_lock<std::mutex> lock(mutex_);
    entered_ = true;
    condition_.notify_all();
    condition_.wait(lock, [this] { return released_; });
  }

  void Flush() override {}
  std::uint32_t LogSize() override { return 0; }

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

#if defined(HAVE_SYS_WAIT_H) && defined(HAVE_UNISTD_H) && \
    defined(GTEST_HAS_DEATH_TEST)
namespace {

// Ends a death test child that deadlocks before the CTest timeout ends the
// test, yet leaves ample room for stalls on a loaded machine.
void ArmDeadlockAlarm() {
  using namespace std::chrono_literals;
  constexpr std::chrono::seconds kDeadlockTimeout = 20s;
  alarm(static_cast<unsigned>(kDeadlockTimeout.count()));
}

}  // namespace
#endif

class ReentrantLogSink : public LogSink {
 public:
  // Joining the thread deadlocks if a sink registry lock is held during the
  // callback. CTest reports such a hang by timing out the test.
  void send(LogSeverity /* severity */, const char* /* full_filename */,
            const char* /* base_filename */, int /* line */,
            const LogMessageTime& /* time */, const char* /* message */,
            std::size_t /* message_len */) override {
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
            const char*, std::size_t) override {
    RemoveLogSink(this);
  }
};

namespace {

// Lets a fixed number of threads meet inside a sink callback, so that each
// of them is dispatching to the sink when the first one leaves.
class SinkCallBarrier {
 public:
  explicit SinkCallBarrier(int parties) : remaining_(parties) {}

  void ArriveAndWait() {
    std::unique_lock<std::mutex> lock{mutex_};
    if (--remaining_ == 0) {
      cond_.notify_all();
    }
    cond_.wait(lock, [this] { return remaining_ == 0; });
  }

 private:
  std::mutex mutex_;
  std::condition_variable cond_;
  int remaining_;
};

constexpr int kConcurrentSinkCallers = 2;

class ConcurrentlySelfRemovingLogSink : public LogSink {
 public:
  void send(LogSeverity, const char*, const char*, int, const LogMessageTime&,
            const char*, std::size_t) override {
    barrier_.ArriveAndWait();
    RemoveLogSink(this);
  }

 private:
  SinkCallBarrier barrier_{kConcurrentSinkCallers};
};

class OtherSinkRemovingLogSink : public LogSink {
 public:
  explicit OtherSinkRemovingLogSink(LogSink* other) : other_(other) {}

  void send(LogSeverity, const char*, const char*, int, const LogMessageTime&,
            const char*, std::size_t) override {
    barrier_.ArriveAndWait();
    RemoveLogSink(other_);
  }

 private:
  LogSink* other_;
  SinkCallBarrier barrier_{kConcurrentSinkCallers};
};

class QuietLogSink : public LogSink {
 public:
  void send(LogSeverity, const char*, const char*, int, const LogMessageTime&,
            const char*, std::size_t) override {}
};

class ThrowingLogSink : public LogSink {
 public:
  void send(LogSeverity, const char*, const char*, int, const LogMessageTime&,
            const char*, std::size_t) override {
    throw std::runtime_error("sink failure");
  }
};

#if defined(HAVE_SYS_WAIT_H) && defined(HAVE_UNISTD_H) && \
    defined(GTEST_HAS_DEATH_TEST)
void LogFromConcurrentThreads() {
  std::vector<std::thread> threads;
  for (int i = 0; i < kConcurrentSinkCallers; ++i) {
    threads.emplace_back([] { LOG(INFO) << "Log to a concurrent sink."; });
  }
  for (auto& thread : threads) {
    thread.join();
  }
}
#endif

}  // namespace

class ReentrantLogger : public base::Logger {
 public:
  void Write(bool, const std::chrono::system_clock::time_point&, const char*,
             std::size_t) override {
    if (!nested_) {
      nested_ = true;
      LOG(INFO) << "Nested custom logger message.";
    }
  }

  void Flush() override {}
  std::uint32_t LogSize() override { return 0; }

 private:
  bool nested_{false};
};

void TestLogSink() {
  TestLogSinkImpl sink;
  LogSink* no_sink = nullptr;

  LOG_TO_SINK(&sink, INFO) << "LOG_TO_SINK: "
                           << "collected info";
  LOG_TO_SINK(&sink, WARNING) << "LOG_TO_SINK: "
                              << "collected warning";
  LOG_TO_SINK(&sink, ERROR) << "LOG_TO_SINK: "
                            << "collected error";

  LOG_TO_SINK(no_sink, INFO) << "LOG_TO_SINK: "
                             << "reported info";
  LOG_TO_SINK(no_sink, WARNING) << "LOG_TO_SINK: "
                                << "reported warning";
  LOG_TO_SINK(nullptr, ERROR) << "LOG_TO_SINK: "
                              << "reported error";

  LOG_TO_SINK_BUT_NOT_TO_LOGFILE(&sink, INFO)
      << "LOG_TO_SINK_BUT_NOT_TO_LOGFILE: "
      << "collected info";
  LOG_TO_SINK_BUT_NOT_TO_LOGFILE(&sink, WARNING)
      << "LOG_TO_SINK_BUT_NOT_TO_LOGFILE: "
      << "collected warning";
  LOG_TO_SINK_BUT_NOT_TO_LOGFILE(&sink, ERROR)
      << "LOG_TO_SINK_BUT_NOT_TO_LOGFILE: "
      << "collected error";

  LOG_TO_SINK_BUT_NOT_TO_LOGFILE(no_sink, INFO)
      << "LOG_TO_SINK_BUT_NOT_TO_LOGFILE: "
      << "thrashed info";
  LOG_TO_SINK_BUT_NOT_TO_LOGFILE(no_sink, WARNING)
      << "LOG_TO_SINK_BUT_NOT_TO_LOGFILE: "
      << "thrashed warning";
  LOG_TO_SINK_BUT_NOT_TO_LOGFILE(nullptr, ERROR)
      << "LOG_TO_SINK_BUT_NOT_TO_LOGFILE: "
      << "thrashed error";

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

namespace {

// Removes itself from within the callback of two threads at different times.
class TwiceRemovingSink : public LogSink {
 public:
  void send(LogSeverity /* severity */, const char* /* full_filename */,
            const char* /* base_filename */, int /* line */,
            const LogMessageTime& /* time */, const char* /* message */,
            std::size_t /* message_len */) override {
    // Gives the owner time to wait for the calls before the second removal
    // and lets the first callback outlast the removal.
    constexpr std::chrono::milliseconds kSecondRemovalDelay{100};
    constexpr std::chrono::milliseconds kFirstCallbackTail{200};
    inside.fetch_add(1);
    const int index = arrivals.fetch_add(1);
    while (arrivals.load() < kCallbacks) {
      std::this_thread::yield();
    }
    if (index == 0) {
      RemoveLogSink(this);
      std::this_thread::sleep_for(kFirstCallbackTail);
    } else {
      std::this_thread::sleep_for(kSecondRemovalDelay);
      RemoveLogSink(this);
    }
    inside.fetch_sub(1);
  }

  static constexpr int kCallbacks = 2;
  std::atomic<int> arrivals{0};
  std::atomic<int> inside{0};
};

}  // namespace

// RemoveLogSink() called from outside the callbacks returns only after every
// callback has left, even while callbacks remove the sink themselves. The
// alarm turns a deadlock into a failure instead of a hang.
TEST(Logging, RemoveLogSinkWaitsForCallbacksThatRemoveTheSink) {
#if defined(HAVE_SYS_WAIT_H) && defined(HAVE_UNISTD_H) && \
    defined(GTEST_HAS_DEATH_TEST)
  ASSERT_EXIT(
      {
        ArmDeadlockAlarm();
        nglog::SetFlag(FLAGS_logtostderr, true);
        TwiceRemovingSink sink;
        AddLogSink(&sink);
        std::thread first{[] { LOG(INFO) << "first"; }};
        std::thread second{[] { LOG(INFO) << "second"; }};
        while (sink.arrivals.load() < TwiceRemovingSink::kCallbacks) {
          std::this_thread::yield();
        }
        RemoveLogSink(&sink);
        const bool callbacks_left = sink.inside.load() == 0;
        first.join();
        second.join();
        _exit(callbacks_left ? EXIT_SUCCESS : EXIT_FAILURE);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
#else
  GTEST_SKIP() << "requires death tests with alarm()";
#endif
}

namespace {

// Blocks the first call until released.
class BlockingLogSink : public LogSink {
 public:
  void send(LogSeverity /* severity */, const char* /* full_filename */,
            const char* /* base_filename */, int /* line */,
            const LogMessageTime& /* time */, const char* /* message */,
            std::size_t /* message_len */) override {
    std::unique_lock<std::mutex> lock{mutex_};
    entered_ = true;
    condition_.notify_all();
    condition_.wait(lock, [this] { return released_; });
  }

  void WaitUntilEntered() {
    std::unique_lock<std::mutex> lock{mutex_};
    condition_.wait(lock, [this] { return entered_; });
  }

  void Release() {
    {
      const std::lock_guard<std::mutex> lock{mutex_};
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

// Counts the calls made after its owner removed it.
class RemovedCallCountingLogSink : public LogSink {
 public:
  void send(LogSeverity /* severity */, const char* /* full_filename */,
            const char* /* base_filename */, int /* line */,
            const LogMessageTime& /* time */, const char* /* message */,
            std::size_t /* message_len */) override {
    if (removed.load()) {
      calls_after_removal.fetch_add(1);
    }
  }

  std::atomic<bool> removed{false};
  std::atomic<int> calls_after_removal{0};
};

}  // namespace

// A thread that dispatches a message keeps the sinks registered when it
// started. A sink removed meanwhile must not be called anymore, since its
// owner may have destroyed it once RemoveLogSink() returned.
TEST(Logging, RemovedSinkIsNotCalledByDispatchInProgress) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, true);
  ScopedStreamCapture capture{StandardStream::kStderr};
  BlockingLogSink blocking;
  RemovedCallCountingLogSink removed;
  // Sinks are called in the reverse order of their registration.
  AddLogSink(&removed);
  AddLogSink(&blocking);

  std::thread logger{[] { LOG(INFO) << "dispatched during removal"; }};
  blocking.WaitUntilEntered();
  RemoveLogSink(&removed);
  removed.removed.store(true);
  blocking.Release();
  logger.join();
  RemoveLogSink(&blocking);

  EXPECT_EQ(removed.calls_after_removal.load(), 0);
}

TEST(Logging, RegisteredSinkCanRemoveItself) {
#if defined(HAVE_SYS_WAIT_H) && defined(HAVE_UNISTD_H) && \
    defined(GTEST_HAS_DEATH_TEST)
  ASSERT_EXIT(
      {
        ArmDeadlockAlarm();
        SelfRemovingLogSink sink;
        AddLogSink(&sink);
        LOG(INFO) << "Remove sink from callback.";
        _exit(EXIT_SUCCESS);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
#endif
}

TEST(Logging, RegisteredSinkCanRemoveItselfFromConcurrentCalls) {
#if defined(HAVE_SYS_WAIT_H) && defined(HAVE_UNISTD_H) && \
    defined(GTEST_HAS_DEATH_TEST)
  ASSERT_EXIT(
      {
        ArmDeadlockAlarm();
        ConcurrentlySelfRemovingLogSink sink;
        AddLogSink(&sink);
        LogFromConcurrentThreads();
        _exit(EXIT_SUCCESS);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
#endif
}

TEST(Logging, RegisteredSinkCanRemoveAnotherSinkFromConcurrentCalls) {
#if defined(HAVE_SYS_WAIT_H) && defined(HAVE_UNISTD_H) && \
    defined(GTEST_HAS_DEATH_TEST)
  ASSERT_EXIT(
      {
        ArmDeadlockAlarm();
        QuietLogSink other;
        OtherSinkRemovingLogSink remover{&other};
        // Sinks run in reverse order of registration, so the remover runs
        // before the sink it removes.
        AddLogSink(&other);
        AddLogSink(&remover);
        LogFromConcurrentThreads();
        RemoveLogSink(&remover);
        _exit(EXIT_SUCCESS);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
#endif
}

TEST(Logging, ThrowingSinkDoesNotBlockRemovalOfLaterSinks) {
#if defined(HAVE_SYS_WAIT_H) && defined(HAVE_UNISTD_H) && \
    defined(GTEST_HAS_DEATH_TEST)
  ASSERT_EXIT(
      {
        ArmDeadlockAlarm();
        QuietLogSink quiet;
        ThrowingLogSink thrower;
        AddLogSink(&quiet);
        AddLogSink(&thrower);
        try {
          LOG(INFO) << "Log to a throwing sink.";
        } catch (const std::runtime_error&) {
        }
        RemoveLogSink(&thrower);
        std::thread{[&quiet] { RemoveLogSink(&quiet); }}.join();
        _exit(EXIT_SUCCESS);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
#endif
}

#if defined(GTEST_HAS_DEATH_TEST)
namespace {

std::atomic<bool> g_message_buffer_allocated{false};

// Records an allocation that can hold a message buffer. Other allocations
// depend on the standard library, such as the proxies that checked iterators
// of MSVC allocate for every string.
void RecordMessageBufferAllocation(std::size_t size) {
  if (size >= LogMessage::kMaxLogMessageLen) {
    g_message_buffer_allocated.store(true, std::memory_order_relaxed);
  }
}

}  // namespace
#endif

TEST(Logging, ThrowingSinkReleasesThreadMessageBuffer) {
#if defined(GTEST_HAS_DEATH_TEST)
  ASSERT_EXIT(
      {
        nglog::SetFlag(FLAGS_logtostderr, true);
        QuietLogSink quiet;
        {
          ThrowingLogSink thrower;
          AddLogSink(&thrower);
          try {
            LOG(INFO) << "Log to a throwing sink.";
          } catch (const std::runtime_error&) {
          }
          RemoveLogSink(&thrower);
        }
        // Messages use a preallocated buffer of the thread as long as no
        // other message of the thread holds it.
        g_new_size_hook = &RecordMessageBufferAllocation;
        LOG_TO_SINK_BUT_NOT_TO_LOGFILE(&quiet, INFO)
            << "Uses the thread buffer.";
        g_new_size_hook = nullptr;
        std::exit(g_message_buffer_allocated.load(std::memory_order_relaxed)
                      ? EXIT_FAILURE
                      : EXIT_SUCCESS);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
#endif
}

TEST(Logging, CustomLoggerCanLogDuringWrite) {
#if defined(HAVE_SYS_WAIT_H) && defined(HAVE_UNISTD_H) && \
    defined(GTEST_HAS_DEATH_TEST)
  ASSERT_EXIT(
      {
        ArmDeadlockAlarm();
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

#if defined(HAVE_SYS_WAIT_H) && defined(HAVE_UNISTD_H) && \
    defined(GTEST_HAS_DEATH_TEST)
namespace {

// Forwards to the default file logger from another thread and waits for it,
// as asynchronous loggers do.
class ForwardingLogger : public base::Logger {
 public:
  explicit ForwardingLogger(base::Logger* wrapped) : wrapped_{wrapped} {}

  void Write(bool force_flush,
             const std::chrono::system_clock::time_point& timestamp,
             const char* message, std::size_t message_len) override {
    std::thread worker{[this, force_flush, &timestamp, message, message_len] {
      wrapped_->Write(force_flush, timestamp, message, message_len);
    }};
    worker.join();
  }

  void Flush() override {
    std::thread worker{[this] { wrapped_->Flush(); }};
    worker.join();
  }

  std::uint32_t LogSize() override { return wrapped_->LogSize(); }

 private:
  base::Logger* wrapped_;
};

}  // namespace
#endif

static void DeleteFiles(const string& pattern);

TEST(Logging, LoggerCanForwardToTheDefaultLoggerFromAnotherThread) {
#if defined(HAVE_SYS_WAIT_H) && defined(HAVE_UNISTD_H) && \
    defined(GTEST_HAS_DEATH_TEST)
  const std::string dest = TestTmpDir() + "/logging_test_forwarding_logger.";
  ASSERT_EXIT(
      {
        ArmDeadlockAlarm();
        nglog::SetFlag(FLAGS_logtostderr, false);
        nglog::SetFlag(FLAGS_logtostdout, false);
        SetLogDestination(NGLOG_INFO, dest.c_str());
        auto logger =
            std::make_unique<ForwardingLogger>(base::GetLogger(NGLOG_INFO));
        base::SetLogger(NGLOG_INFO, logger.release());
        LOG(INFO) << "Log through a forwarding logger.";
        FlushLogFiles(NGLOG_INFO);
        _exit(EXIT_SUCCESS);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
  DeleteFiles(dest + "*");
#else
  GTEST_SKIP() << "requires death tests with alarm()";
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

TEST(LockMetrics, UncontendedAcquisitionsAreNotCountedAsContended) {
  constexpr int kAcquisitions = 1000;
  internal::ResetLockMetrics();
  internal::FileMutex mutex;
  for (int i = 0; i < kAcquisitions; ++i) {
    std::lock_guard<internal::FileMutex> lock{mutex};
  }

  const internal::LockMetrics metrics =
      internal::GetLockMetrics(internal::LockKind::kFile);
  EXPECT_EQ(metrics.acquisitions, static_cast<std::uint64_t>(kAcquisitions));
  EXPECT_EQ(metrics.contended_acquisitions, 0U);
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
            std::size_t message_len) override {
    messages.emplace_back(message, message_len);
  }
};

class CountingLogSink : public LogSink {
 public:
  void send(LogSeverity, const char*, const char*, int, const LogMessageTime&,
            const char*, std::size_t) override {
    count_.fetch_add(1, std::memory_order_relaxed);
  }

  int count() const { return count_.load(std::memory_order_relaxed); }

 private:
  std::atomic<int> count_{0};
};

constexpr int kCountingThreads = 8;
constexpr int kCountingIterations = 20'000;

/**
 * Calls @p log @p iterations times from each of several threads at once and
 * counts the messages that reach a registered sink. Log files are disabled to
 * keep the test fast.
 */
template <class Log>
int CountConcurrentMessages(Log log, int iterations = kCountingIterations) {
  auto save = nglog::MakeScopedFlags(
      nglog::MakeScopedFlag(FLAGS_logtostderr, false),
      nglog::MakeScopedFlag(FLAGS_logtostdout, false),
      nglog::MakeScopedFlag(FLAGS_alsologtostderr, false));
  SetLogDestination(NGLOG_INFO, "");
  CountingLogSink sink;
  AddLogSink(&sink);

  std::atomic<int> waiting{kCountingThreads};
  std::vector<std::thread> threads;
  for (int i = 0; i < kCountingThreads; ++i) {
    threads.emplace_back([&waiting, &log, iterations] {
      waiting.fetch_sub(1, std::memory_order_acq_rel);
      while (waiting.load(std::memory_order_acquire) > 0) {
      }
      for (int j = 0; j < iterations; ++j) {
        log();
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  RemoveLogSink(&sink);
  LogToStderr();
  return sink.count();
}

// The number of an occurrence wraps around to zero instead of overflowing
// once a call site ran more often than its counter can count.
TEST(LogMacros, OccasionalVariantsWrapAroundTheirCounter) {
  constexpr unsigned kLargestCount = std::numeric_limits<unsigned>::max();
  nglog::internal::EveryNCounters every_n;
  every_n.occurrences.store(kLargestCount);
  EXPECT_EQ(nglog::internal::CountEveryN(every_n, 1).number, 0U);
  nglog::internal::EveryNCounters if_every_n;
  if_every_n.occurrences.store(kLargestCount);
  EXPECT_EQ(nglog::internal::CountIfEveryN(if_every_n, true, 1).number, 0U);
}

// The largest limit still stops counting once it has been reached.
TEST(LogMacros, FirstNCountsUpToTheLargestLimit) {
  constexpr int kLargestInt = std::numeric_limits<int>::max();
  std::atomic<unsigned> occurrences{static_cast<unsigned>(kLargestInt)};
  const auto occurrence =
      nglog::internal::CountFirstNOccurrence(occurrences, kLargestInt);
  EXPECT_TRUE(occurrence.skipped);
  EXPECT_EQ(occurrence.number, static_cast<unsigned>(kLargestInt) + 1U);
}

// The steady clock may start at system boot, so the first occurrence logs
// even if less time than the period has passed since then.
TEST(LogMacros, EveryTLogsTheFirstOccurrence) {
  std::atomic<std::int64_t> previous{0};
  const auto since_epoch = std::chrono::steady_clock::now().time_since_epoch();
  const auto longer_than_since_epoch =
      std::chrono::duration_cast<std::chrono::nanoseconds>(since_epoch) +
      std::chrono::hours{1};
  EXPECT_TRUE(nglog::internal::ClaimEveryT(previous, longer_than_since_epoch));
  EXPECT_FALSE(nglog::internal::ClaimEveryT(previous, longer_than_since_epoch));
}

// Every call logging would hide lost or duplicated occurrences.
TEST(LogMacros, EveryNCountsConcurrentOccurrencesExactly) {
  constexpr int kPeriod = 2;
  EXPECT_EQ(CountConcurrentMessages(
                [] { LOG_EVERY_N(INFO, kPeriod) << "every other occurrence"; }),
            kCountingThreads * kCountingIterations / kPeriod);
}

TEST(LogMacros, IfEveryNCountsConcurrentOccurrencesExactly) {
  constexpr int kPeriod = 2;
  EXPECT_EQ(CountConcurrentMessages([] {
              LOG_IF_EVERY_N(INFO, true, kPeriod) << "every other occurrence";
            }),
            kCountingThreads * kCountingIterations / kPeriod);
}

// Each instantiation has a call site of its own, so that threads race for the
// first occurrence many times.
template <std::size_t>
struct FirstOccurrence {
  static void Log() { LOG_FIRST_N(INFO, 1) << "first occurrence"; }
};

template <std::size_t>
struct HourlyOccurrence {
  static void Log() {
    using namespace std::chrono_literals;
    LOG_EVERY_T(INFO, std::chrono::duration<double>{1h}.count())
        << "hourly occurrence";
  }
};

constexpr std::size_t kCallSites = 256;
// Each call site logs during the first pass over the call sites only, so later
// passes merely slow the test down.
constexpr int kSinglePass = 1;

template <template <std::size_t> class Site, std::size_t... Sites>
void LogAtCallSites(std::index_sequence<Sites...> /*sites*/) {
  const int expand[] = {(Site<Sites>::Log(), 0)...};
  static_cast<void>(expand);
}

TEST(LogMacros, FirstNLogsExactlyNConcurrentOccurrences) {
  EXPECT_EQ(CountConcurrentMessages(
                [] {
                  LogAtCallSites<FirstOccurrence>(
                      std::make_index_sequence<kCallSites>{});
                },
                kSinglePass),
            static_cast<int>(kCallSites));
}

// Occurrences arriving together within a period log once.
TEST(LogMacros, EveryTLogsConcurrentOccurrencesOncePerPeriod) {
  EXPECT_EQ(CountConcurrentMessages(
                [] {
                  LogAtCallSites<HourlyOccurrence>(
                      std::make_index_sequence<kCallSites>{});
                },
                kSinglePass),
            static_cast<int>(kCallSites));
}

TEST(Logging, EmptyMessageWithoutPrefixIsSentAsEmptyMessage) {
  MessageCollectingSink sink;
  {
    LogMessage message(__FILE__, LogMessage::kNoLogPrefix, NGLOG_INFO, &sink,
                       /*also_send_to_log=*/false);
  }
  EXPECT_THAT(sink.messages, ElementsAre(""));
}

TEST(LogMacros, ErrnoAndPeriodicVariantsLog) {
  nglog::ScopedFlags<> save;
  RouteLogMessagesTo(StandardStream::kStderr);
  nglog::SetFlag(FLAGS_v, 1);
  constexpr int kPeriod = 2;
  constexpr int kOccurrences = 4;
  MessageCollectingSink sink;
  AddLogSink(&sink);
  {
    ScopedStreamCapture capture{StandardStream::kStderr};
    errno = ENOENT;
    PLOG_IF(INFO, true) << "plog_if";
    PLOG_IF(INFO, false) << "not logged";
    LOG_ASSERT(true);
    for (int i = 0; i < kOccurrences; ++i) {
      VLOG_EVERY_N(1, kPeriod) << "vlog every 2";
      DLOG_EVERY_N(INFO, kPeriod) << "dlog every 2";
    }
  }
  RemoveLogSink(&sink);

  const auto plog_message = AllOf(
      StartsWith("plog_if: "), EndsWith(" [" + std::to_string(ENOENT) + "]"));
#if DCHECK_IS_ON()
  EXPECT_THAT(sink.messages,
              ElementsAre(plog_message, "vlog every 2", "dlog every 2",
                          "vlog every 2", "dlog every 2"));
#else
  EXPECT_THAT(sink.messages,
              ElementsAre(plog_message, "vlog every 2", "vlog every 2"));
#endif
}

TEST(LogMacros, OccasionalVariantsCanBeJumpedAcross) {
  // LOG_FIRST_N() logs during the first run of the test only.
  static bool first_run = true;
  const bool first_n_logs = std::exchange(first_run, false);
  nglog::ScopedFlags<> save;
  RouteLogMessagesTo(StandardStream::kStderr);
  constexpr int kCases = 2;
  MessageCollectingSink sink;
  AddLogSink(&sink);
  {
    ScopedStreamCapture capture{StandardStream::kStderr};
    for (int selected = 0; selected < kCases; ++selected) {
      switch (selected) {
        case 0:
          LOG_EVERY_N(INFO, 1) << "every n";
          LOG_IF_EVERY_N(INFO, true, 1) << "if every n";
          PLOG_EVERY_N(INFO, 1) << "plog every n";
          LOG_FIRST_N(INFO, 1) << "first n";
          break;
        case 1:
          LOG(INFO) << "jumped across";
          break;
      }
    }
    goto skipped;
    LOG_EVERY_N(INFO, 1) << "skipped every n";
    LOG_IF_EVERY_N(INFO, true, 1) << "skipped if every n";
    PLOG_EVERY_N(INFO, 1) << "skipped plog every n";
    LOG_FIRST_N(INFO, 1) << "skipped first n";
  skipped:;
  }
  RemoveLogSink(&sink);

  std::vector<LineMatcher> expected{"every n", "if every n",
                                    StartsWith("plog every n: ")};
  if (first_n_logs) {
    expected.emplace_back("first n");
  }
  expected.emplace_back("jumped across");
  EXPECT_THAT(sink.messages, ElementsAreArray(expected));
}

TEST(LogMacros, OccasionalVariantsLeaveTrailingElseToEnclosingIf) {
  // LOG_FIRST_N() and LOG_EVERY_T() log during the first run of the test
  // only.
  static bool first_run = true;
  const bool first_n_logs = std::exchange(first_run, false);
  nglog::ScopedFlags<> save;
  RouteLogMessagesTo(StandardStream::kStderr);
  constexpr int kPeriod = 2;
  constexpr double kLongIntervalSeconds = 3600;
  constexpr int kOccurrences = 4;
  const bool enabled = true;
  int else_taken = 0;
  MessageCollectingSink sink;
  AddLogSink(&sink);
  {
    ScopedStreamCapture capture{StandardStream::kStderr};
    for (int i = 0; i < kOccurrences; ++i) {
      // The missing braces are the subject of this test.
      // NOLINTBEGIN(google-readability-braces-around-statements,readability-braces-around-statements)
      if (enabled)
        LOG_EVERY_N(INFO, kPeriod) << "every n";
      else
        ++else_taken;
      if (enabled)
        LOG_IF_EVERY_N(INFO, true, kPeriod) << "if every n";
      else
        ++else_taken;
      if (enabled)
        PLOG_EVERY_N(INFO, kPeriod) << "plog every n";
      else
        ++else_taken;
      if (enabled)
        LOG_FIRST_N(INFO, 1) << "first n";
      else
        ++else_taken;
      if (enabled)
        LOG_EVERY_T(INFO, kLongIntervalSeconds) << "every t";
      else
        ++else_taken;
      // NOLINTEND(google-readability-braces-around-statements,readability-braces-around-statements)
    }
  }
  RemoveLogSink(&sink);

  EXPECT_EQ(else_taken, 0);
  std::vector<LineMatcher> expected{"every n", "if every n",
                                    StartsWith("plog every n: ")};
  if (first_n_logs) {
    expected.emplace_back("first n");
    expected.emplace_back("every t");
  }
  expected.insert(expected.end(),
                  {"every n", "if every n", StartsWith("plog every n: ")});
  EXPECT_THAT(sink.messages, ElementsAreArray(expected));
}

#ifdef GTEST_HAS_DEATH_TEST
// Each death symbolizes a stack trace, which takes long with some backends,
// so the deaths are spread over several tests. The parentheses keep the
// commas of the expanded checks inside one macro argument.
TEST(DeathCheckVariants, ReportNumericFailures) {
  constexpr double kTolerance = 0.5;
  constexpr double kWithinTolerance = 1.25;
  constexpr double kBeyondTolerance = 2.0;
  constexpr double kUnequal = 1.5;
  constexpr int kIndexCount = 3;
  const double one = 1.0;
  const int indices[kIndexCount] = {};
  static_cast<void>(indices);
  CHECK_DOUBLE_EQ(one, 1.0);
  CHECK_NEAR(one, kWithinTolerance, kTolerance);
  CHECK_INDEX(kIndexCount - 1, indices);

  ASSERT_DEATH(([one] { CHECK_DOUBLE_EQ(one, kUnequal); }()), "Check failed");
  ASSERT_DEATH(([one] { CHECK_NEAR(one, kBeyondTolerance, kTolerance); }()),
               "Check failed");
  ASSERT_DEATH(([] { CHECK_INDEX(kIndexCount, indices); }()), "Check failed");
}

TEST(DeathCheckVariants, ReportAssertionFailures) {
  const double one = 1.0;
  PCHECK(one == 1.0);

  ASSERT_DEATH(([one] { LOG_ASSERT(one == 2.0); }()),
               "Assert failed: one == 2.0");
  ASSERT_DEATH(
      ([one] {
        errno = ENOENT;
        PCHECK(one == 2.0);
      }()),
      "Check failed: one == 2.0 : .* \\[" + std::to_string(ENOENT) + "\\]");
#  if DCHECK_IS_ON()
  ASSERT_DEATH(([] { DCHECK_STREQ("a", "b"); }()), "CHECK_STREQ failed");
#  endif
}
#endif

TEST(Logging, SetStderrLoggingSelectsTheMinimumSeverity) {
  nglog::ScopedFlags<> save;
  RouteLogMessagesTo(StandardStream::kStderr);
  nglog::SetFlag(FLAGS_logtostderr, false);
  for (int severity = 0; severity < NUM_SEVERITIES; ++severity) {
    SetLogDestination(static_cast<LogSeverity>(severity), "");
  }
  SetStderrLogging(NGLOG_WARNING);

  ScopedStreamCapture capture{StandardStream::kStderr};
  LOG(INFO) << "below the threshold";
  LOG(WARNING) << "at the threshold";
  EXPECT_THAT(SplitLines(capture.Stop()),
              ElementsAre(LogLine('W', "at the threshold")));
}

TEST(Logging, LoggingDirectoriesEndWithSeparator) {
  const std::vector<std::string>& directories = GetLoggingDirectories();
  ASSERT_THAT(directories, Not(IsEmpty()));
  for (const std::string& directory : directories) {
    EXPECT_THAT(directory, AnyOf(EndsWith("/"), EndsWith("\\")));
  }
}

#ifndef NGLOG_OS_WINDOWS
namespace nglog {
void GetExistingTempDirectories(std::vector<std::string>& list);
}  // namespace nglog

// An empty variable names no directory, rather than the root directory.
TEST(Logging, EmptyTemporaryDirectoryVariablesAreSkipped) {
  const ScopedEnvironmentVariable test_tmpdir{"TEST_TMPDIR", ""};
  const ScopedEnvironmentVariable tmpdir{"TMPDIR", ""};
  const ScopedEnvironmentVariable tmp{"TMP", ""};
  std::vector<std::string> directories;
  nglog::GetExistingTempDirectories(directories);
  EXPECT_THAT(directories, Not(Contains("/")));
}
#endif

TEST(Logging, MessageReportsTheFullSourcePath) {
  nglog::ScopedFlags<> save;
  RouteLogMessagesTo(StandardStream::kStderr);
  ScopedStreamCapture capture{StandardStream::kStderr};
  LogMessage message(__FILE__, __LINE__);
  EXPECT_STREQ(message.fullname(), __FILE__);
  EXPECT_STREQ(message.basename(), const_basename(__FILE__));
}

#if defined(NGLOG_OS_LINUX)
// Data races between the writers only fail the test under ThreadSanitizer.
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

#if !defined(NGLOG_OS_WINDOWS) && defined(GTEST_HAS_DEATH_TEST)
namespace {

// Logs a colored message while the program exits.
struct LogsColoredOnDestruction {
  ~LogsColoredOnDestruction() { LOG(ERROR) << "logged during exit"; }
};

}  // namespace

// Objects with static storage duration are destroyed after the thread_local
// objects of the exiting thread, so logging from their destructors must not
// rely on thread_local storage.
TEST(Logging, ColoredLoggingDuringExitWritesTheMessage) {
  EXPECT_EXIT(
      {
        setenv("CLICOLOR_FORCE", "1", 1);
        unsetenv("NO_COLOR");
        nglog::SetFlag(FLAGS_logtostderr, true);
        nglog::SetFlag(FLAGS_colorlogtostderr, true);
        LOG(ERROR) << "logged before exit";
        static LogsColoredOnDestruction logs_on_destruction;
        std::exit(EXIT_SUCCESS);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "logged during exit");
}
#endif

#if !defined(NGLOG_OS_WINDOWS)
TEST(Logging, ReprintFatalMessageDoesNotColorStderr) {
  const ScopedEnvironmentVariable clicolor_force{"CLICOLOR_FORCE", "1"};
  const ScopedEnvironmentVariable no_color{"NO_COLOR", nullptr};

  auto flags = nglog::MakeScopedFlags(
      nglog::MakeScopedFlag(FLAGS_logtostderr, true),
      nglog::MakeScopedFlag(FLAGS_logtostdout, false),
      nglog::MakeScopedFlag(FLAGS_colorlogtostderr, true));
  const ScopedExitOnDFatal exit_on_dfatal{true};
  const ScopedThrowOnFatal throw_on_fatal;

  constexpr char kColorEscape[] = "\033[";
  ScopedStreamCapture fatal_capture{StandardStream::kStderr};
  EXPECT_THROW({ LOG(FATAL) << "fatal message for replay"; }, std::logic_error);
  ASSERT_THAT(fatal_capture.Stop(), HasSubstr(kColorEscape));

  ScopedStreamCapture reprint_capture{StandardStream::kStderr};
  ReprintFatalMessage();
  EXPECT_THAT(reprint_capture.Stop(), Not(HasSubstr(kColorEscape)));
}
#endif

TEST(Logging, ConcurrentFatalMessages) {
  constexpr int kFatalMessageCount = 20;
  const ScopedThrowOnFatal throw_on_fatal;
  const ScopedExitOnDFatal exit_on_dfatal{true};

  const auto write_fatal = [] {
    for (int i = 0; i < kFatalMessageCount; ++i) {
      EXPECT_THROW(
          { LOG(FATAL) << "Concurrent fatal message."; }, std::logic_error);
    }
  };
  std::thread first{write_fatal};
  std::thread second{write_fatal};
  first.join();
  second.join();
}

TEST(Logging, LaterFatalMessagesStartWithEmptyBuffer) {
  constexpr int kFatalMessageCount = 3;
  const ScopedThrowOnFatal throw_on_fatal;
  const ScopedExitOnDFatal exit_on_dfatal{true};
  auto flags = nglog::MakeScopedFlags(
      nglog::MakeScopedFlag(FLAGS_logtostderr, true),
      nglog::MakeScopedFlag(FLAGS_logtostdout, false),
      nglog::MakeScopedFlag(FLAGS_colorlogtostderr, false));
  std::vector<std::string> lines;
  for (int i = 0; i < kFatalMessageCount; ++i) {
    ScopedStreamCapture capture{StandardStream::kStderr};
    EXPECT_THROW({ LOG(FATAL) << "fatal number " << i; }, std::logic_error);
    const std::vector<std::string> captured = SplitLines(capture.Stop());
    ASSERT_THAT(captured, Not(IsEmpty()));
    lines.push_back(captured.front());
  }

  EXPECT_THAT(lines, ElementsAre(LogLine('F', "fatal number 0"),
                                 LogLine('F', "fatal number 1"),
                                 LogLine('F', "fatal number 2")));
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

  auto orig_ptr = std::make_unique<std::int64_t>();
  std::int64_t* ptr = DCHECK_NOTNULL(orig_ptr.get());
  EXPECT_EQ(ptr, orig_ptr.get());
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
  ASSERT_DEATH(CHECK_STREQ(nullptr, "this"),
               "CHECK_STREQ failed: .* \\( vs\\. this\\)");
  ASSERT_DEATH(CHECK_STREQ("this", "siht"),
               "CHECK_STREQ failed: .* \\(this vs\\. siht\\)");
  ASSERT_DEATH(CHECK_STRCASEEQ(nullptr, "siht"),
               "CHECK_STRCASEEQ failed: .* \\( vs\\. siht\\)");
  ASSERT_DEATH(CHECK_STRCASEEQ("this", "siht"),
               "CHECK_STRCASEEQ failed: .* \\(this vs\\. siht\\)");
  ASSERT_DEATH(CHECK_STRNE(nullptr, nullptr),
               "CHECK_STRNE failed: .* \\( vs\\. \\)");
  ASSERT_DEATH(CHECK_STRNE("this", "this"),
               "CHECK_STRNE failed: .* \\(this vs\\. this\\)");
  ASSERT_DEATH(CHECK_STREQ((string("a") + "b").c_str(), "abc"),
               "CHECK_STREQ failed: .* \\(ab vs\\. abc\\)");
}

TEST(DeathCheckOp, ReportsBothValues) {
  // Clang deems captures of constants unnecessary, so the values are mutable.
  int one = 1;
  int two = 2;
  // The parentheses keep the commas of the expanded checks inside one macro
  // argument.
  ASSERT_DEATH(([one, two] { CHECK_EQ(one, two); }()),
               "Check failed: one == two \\(1 vs\\. 2\\)");
  ASSERT_DEATH(([one] { CHECK_NE(one, one); }()),
               "Check failed: one != one \\(1 vs\\. 1\\)");
  ASSERT_DEATH(([one, two] { CHECK_LT(two, one); }()),
               "Check failed: two < one \\(2 vs\\. 1\\)");
  ASSERT_DEATH(([one, two] { CHECK(one == two); }()),
               "Check failed: one == two");
}
#endif

TEST(CheckNOTNULL, Simple) {
  std::int64_t t;
  void* ptr = static_cast<void*>(&t);
  void* ref = CHECK_NOTNULL(ptr);
  EXPECT_EQ(ptr, ref);
  CHECK_NOTNULL(reinterpret_cast<char*>(ptr));
  CHECK_NOTNULL(reinterpret_cast<unsigned char*>(ptr));
  CHECK_NOTNULL(reinterpret_cast<int*>(ptr));
  CHECK_NOTNULL(reinterpret_cast<std::int64_t*>(ptr));
}

#ifdef GTEST_HAS_DEATH_TEST
TEST(DeathCheckNN, Simple) {
  ASSERT_DEATH(CHECK_NOTNULL(static_cast<void*>(nullptr)),
               "'static_cast<void\\*>\\(nullptr\\)' Must be non nullptr");
}
#endif

// Get list of file names that match pattern
static vector<string> GetFiles(const string& pattern) {
  vector<string> files;
#if defined(HAVE_GLOB_H)
  glob_t g{};
  const auto cleanup_fn = [&g] { globfree(&g); };
  const ScopedExit<decltype(cleanup_fn)> cleanup{cleanup_fn};
  const int r = glob(pattern.c_str(), 0, nullptr, &g);
  EXPECT_THAT(r, AnyOf(0, GLOB_NOMATCH)) << "error matching " << pattern;
  files.assign(g.gl_pathv, g.gl_pathv + g.gl_pathc);
#elif defined(NGLOG_OS_WINDOWS)
  std::wstring wide_pattern;
  if (!nglog::internal::Utf8ToWide(pattern.data(), pattern.size(),
                                   &wide_pattern)) {
    ADD_FAILURE() << "cannot convert " << pattern << " to UTF-16";
    return files;
  }
  const std::size_t index = pattern.find_last_of("\\/");
  if (index == string::npos) {
    ADD_FAILURE() << "no directory separator in " << pattern;
    return files;
  }
  const string dirname = pattern.substr(0, index + 1);
  WIN32_FIND_DATAW data;
  const HANDLE find_handle = FindFirstFileW(wide_pattern.c_str(), &data);
  if (find_handle == INVALID_HANDLE_VALUE) {
    // Finding no files is OK.
    return files;
  }
  const std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(&FindClose)>
      handle{find_handle, &FindClose};
  do {
    std::string filename;
    const std::size_t filename_length = std::wcslen(data.cFileName);
    EXPECT_TRUE(nglog::internal::WideToUtf8(data.cFileName, filename_length,
                                            &filename));
    files.push_back(dirname + filename);
  } while (FindNextFileW(handle.get(), &data));
#else
#  error There is no way to do glob.
#endif
  return files;
}

// Delete files matching pattern
static void DeleteFiles(const string& pattern) {
  for (const string& file : GetFiles(pattern)) {
    EXPECT_EQ(unlink(file.c_str()), 0) << file << ": " << strerror(errno);
  }
}

// Returns the contents of the only file whose name starts with `prefix`.
static string ReadLogFile(const string& prefix) {
  const vector<string> files = GetFiles(prefix + "*");
  EXPECT_THAT(files, SizeIs(1)) << "files starting with " << prefix;
  if (files.size() != 1) {
    return string();
  }
  return ReadFileContents(files.front());
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
    if (GetFiles(target + "*").size() != 1) {
      ++missed;
    }
  }

  EXPECT_EQ(missed, 0) << "out of " << kAttempts << " listings";
}
#endif

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

constexpr std::size_t kMegabyteShift = 20U;
constexpr std::size_t kMegabyte = std::size_t{1} << kMegabyteShift;

/**
 * Logs 20000 short messages to `dest` without a timestamp in the file name.
 * Without rotation, the messages add up to about 1.5MB.
 *
 * @return The size of the resulting log file in bytes.
 */
static std::size_t LogSizeAfterWritingMessages(const string& dest) {
  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, false);
  SetLogDestination(NGLOG_INFO, dest.c_str());

  constexpr int kMessageCount = 20'000;
  for (int i = 0; i < kMessageCount; ++i) {
    LOG(INFO) << "Hello world";
  }
  FlushLogFiles(NGLOG_INFO);

  // Close the destination file before checking its size: on Windows, a
  // stat() of a file that still has an open write handle in this same
  // process can report a stale (pre-flush) size, even after fflush().
  LogToStderr();

  struct stat statbuf;
  EXPECT_EQ(stat(dest.c_str(), &statbuf), 0)
      << "failed to determine size of log file " << dest << ": "
      << strerror(errno);
  return static_cast<std::size_t>(statbuf.st_size);
}

TEST(Logging, ApplicationFingerprintAppearsInLogFileHeader) {
  const nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, false);
  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, false);
  nglog::SetFlag(FLAGS_log_file_header, true);
  const std::string dest = TestTmpDir() + "/logging_test_fingerprint";
  DeleteFiles(dest + "*");
  SetApplicationFingerprint("fingerprint-1234");
  SetLogDestination(NGLOG_INFO, dest.c_str());
  LOG(INFO) << "message after the header";
  FlushLogFiles(NGLOG_INFO);
  LogToStderr();
  SetApplicationFingerprint("");

  EXPECT_THAT(
      SplitLines(ReadFileContents(dest)),
      Contains(StartsWith("Application fingerprint: fingerprint-1234")));
  DeleteFiles(dest + "*");
}

TEST(Logging, MaxLogSizeWhenNoTimestamp) {
  // SetLogDestination() is a no-op while FLAGS_logtostderr is true.
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_logtostderr, false);

  fprintf(stderr, "==== Test setting max log size without timestamp\n");
  const string dest = TestTmpDir() + "/logging_test_max_log_size";
  DeleteFiles(dest + "*");

  // Set max log size to 1MB.
  nglog::SetFlag(FLAGS_max_log_size, 1U);

  // If the max_log_size constraint is respected, earlier logs are truncated
  // and the file stays below 1MB, at around 0.5MB.
  EXPECT_LT(LogSizeAfterWritingMessages(dest),
            nglog::GetFlag(FLAGS_max_log_size) << kMegabyteShift);

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

  // A value larger than the 4095MB cap used to be clamped to the 1MB minimum,
  // which would rotate the file after ~1MB. It must instead be capped to the
  // maximum, so the file is allowed to grow past 1MB without rotating.
  constexpr std::uint32_t kAboveCapMegabytes = 8000U;
  nglog::SetFlag(FLAGS_max_log_size, kAboveCapMegabytes);

  // If the oversized limit were floored to 1MB, earlier logs would be
  // truncated and the resulting file would stay below 1MB.
  EXPECT_GT(LogSizeAfterWritingMessages(dest), kMegabyte);

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

  EXPECT_THAT(ReadLogFile(dest), HasSubstr("message to new base"));

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
  EXPECT_THAT(entries, AllOf(Contains(kFilename), Not(Contains(".")),
                             Not(Contains(".."))));

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

  EXPECT_THAT(ReadLogFile(dest),
              HasSubstr("message with configured logfile mode"));

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

  EXPECT_THAT(ReadLogFile(dest), HasSubstr("test preexisting content"));

  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, false);
  SetLogDestination(NGLOG_INFO, dest.c_str());
  LOG(INFO) << "message to new base, appending to preexisting file";
  FlushLogFiles(NGLOG_INFO);

  // if the logging overwrites the file instead of appending it will fail.
  EXPECT_THAT(
      ReadLogFile(dest),
      AllOf(HasSubstr("test preexisting content"),
            HasSubstr("message to new base, appending to preexisting file")));

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

  nglog::SetFlag(FLAGS_timestamp_in_logfile_name, false);

  // A custom prefix formatter is installed (see main). It determines the
  // actual log line format, so the header must not advertise the default
  // format.
  SetLogDestination(NGLOG_INFO, dest.c_str());
  LOG(INFO) << "header with custom prefix formatter";
  FlushLogFiles(NGLOG_INFO);
  // The header was written but does not describe the line format.
  EXPECT_THAT(ReadLogFile(dest), AllOf(HasSubstr("Running duration (h:mm:ss):"),
                                       Not(HasSubstr("Log line format: "))));

  // Release file handle for the destination file to unlock the file in Windows.
  LogToStderr();
  DeleteFiles(dest + "*");

  // Without a custom prefix formatter the header advertises the default
  // format.
  InstallPrefixFormatter(nullptr);
  SetLogDestination(NGLOG_INFO, dest.c_str());
  LOG(INFO) << "header with default prefix";
  FlushLogFiles(NGLOG_INFO);
  EXPECT_THAT(ReadLogFile(dest), HasSubstr("Log line format: [IWEF]"));

  RestoreTestPrefixFormatter();

  // Release file handle for the destination file to unlock the file in Windows.
  LogToStderr();
  DeleteFiles(dest + "*");
}

TEST(Logging, TwoProcessesWrite) {
// The implementation relies on advisory file locking. Emscripten cannot fork.
#if (defined(HAVE_SYS_WAIT_H) && defined(HAVE_UNISTD_H) &&    \
     defined(HAVE_FCNTL) && !defined(NGLOG_OS_EMSCRIPTEN)) || \
    defined(NGLOG_OS_WINDOWS)
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
  constexpr char kParentMessage[] = "message to new base, parent";
  constexpr char kChildMessage[] =
      "message to new base, child - should only appear on STDERR not on the "
      "file";
  LOG(INFO) << kParentMessage;
  FlushLogFiles(NGLOG_INFO);

#  ifdef NGLOG_OS_WINDOWS
  const WindowsHandle process_handle =
      SpawnSelf("--nglog-log-child \"" + dest + '"');
  ASSERT_TRUE(process_handle);
  EXPECT_EQ(WaitForSingleObject(process_handle.get(),
                                kChildProcessTimeoutMilliseconds),
            WAIT_OBJECT_0);
  DWORD exit_code = 0;
  EXPECT_TRUE(GetExitCodeProcess(process_handle.get(), &exit_code));
  EXPECT_EQ(exit_code, 0U);
#  else
  ScopedStreamCapture capture{StandardStream::kStderr};
  const pid_t pid = fork();
  ASSERT_NE(pid, -1) << strerror(errno);
  if (pid == 0) {
    LOG(INFO) << kChildMessage;
    ShutdownLogging();  // for children proc
    exit(EXIT_SUCCESS);
  }
  wait(nullptr);
  EXPECT_THAT(capture.Stop(),
              MatchesRegex("Could not create log file '[^']+': [^\n]*\n"));
#  endif

  EXPECT_THAT(ReadLogFile(dest),
              AllOf(HasSubstr(kParentMessage), Not(HasSubstr(kChildMessage))));

  // Release file handle for the destination file to unlock the file in Windows.
  LogToStderr();
  DeleteFiles(dest + "*");
#endif
}

// A severity below the first one flushes the files of every severity.
TEST(SeverityRange, FlushingBelowTheFirstSeverityFlushesAll) {
  // LogToStderr() at the end also changes --stderrthreshold.
  const nglog::ScopedFlags<> save_all;
  constexpr std::int32_t kLongBufferSeconds = 3600;
  auto save = nglog::MakeScopedFlags(
      nglog::MakeScopedFlag(FLAGS_logtostderr, false),
      nglog::MakeScopedFlag(FLAGS_logtostdout, false),
      nglog::MakeScopedFlag(FLAGS_logbuflevel, 0),
      nglog::MakeScopedFlag(FLAGS_logbufsecs, kLongBufferSeconds));
  const auto below_first = static_cast<LogSeverity>(-1);
  const std::string dest = TestTmpDir() + "/logging_test_flush_below_first.";
  DeleteFiles(dest + "*");
  SetLogDestination(NGLOG_INFO, dest.c_str());
  const auto contents = [&dest] {
    std::string text;
    for (const std::string& file : GetFiles(dest + "*")) {
      text += ReadFileContents(file);
    }
    return text;
  };

  // The first message creates the file, and the second one stays buffered.
  LOG(INFO) << "creates the file";
  LOG(INFO) << "flushed by FlushLogFiles";
  FlushLogFiles(below_first);
  EXPECT_THAT(contents(), HasSubstr("flushed by FlushLogFiles"));
  LOG(INFO) << "flushed by FlushLogFilesUnsafe";
  FlushLogFilesUnsafe(below_first);
  EXPECT_THAT(contents(), HasSubstr("flushed by FlushLogFilesUnsafe"));

  LogToStderr();
  DeleteFiles(dest + "*");
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

  const WindowsHandle process_handle =
      SpawnSelf("--nglog-log-child \"" + dest + "\" \"" + ready + '"');
  ASSERT_TRUE(process_handle);
  ASSERT_TRUE(WaitForFile(ready));

  WindowsHandle file_handle{
      CreateFileA(dest.c_str(), GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
  ASSERT_NE(file_handle.get(), INVALID_HANDLE_VALUE);

  OVERLAPPED region = MakeLogFileLockRegion();
  const bool lock_succeeded = TryLockLogFile(file_handle.get(), &region);
  EXPECT_FALSE(lock_succeeded);
  if (lock_succeeded) {
    EXPECT_TRUE(
        UnlockFileEx(file_handle.get(), 0, kLogFileLockLength, 0, &region));
  }

  EXPECT_TRUE(TerminateProcess(process_handle.get(), 0));
  EXPECT_EQ(WaitForSingleObject(process_handle.get(),
                                kChildProcessTimeoutMilliseconds),
            WAIT_OBJECT_0);

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
  constexpr std::size_t kOversizedFileSize = kMegabyte + 1U;
  DeleteFiles(dest + "*");

  std::string original_contents(kOversizedFileSize, 'x');
  original_contents.replace(0, preserved_prefix.size(), preserved_prefix);
  std::ofstream output{dest, std::ios::binary};
  output.write(original_contents.data(),
               static_cast<std::streamsize>(original_contents.size()));
  output.close();

  const WindowsHandle process_handle =
      SpawnSelf("--nglog-hold-log-lock \"" + dest + "\" \"" + ready + '"');
  ASSERT_TRUE(process_handle);
  ASSERT_TRUE(WaitForFile(ready));

  SetLogDestination(NGLOG_INFO, dest.c_str());
  ScopedStreamCapture capture{StandardStream::kStderr};
  LOG(INFO) << "message that cannot be written while the file is locked";
  EXPECT_THAT(capture.Stop(), HasSubstr("Could not create log file '"));

  EXPECT_TRUE(TerminateProcess(process_handle.get(), 0));
  EXPECT_EQ(WaitForSingleObject(process_handle.get(),
                                kChildProcessTimeoutMilliseconds),
            WAIT_OBJECT_0);
  EXPECT_TRUE(WaitForLogFileUnlock(dest));

  EXPECT_EQ(ReadFileContents(dest), original_contents);

  LogToStderr();
  DeleteFiles(dest + "*");
#endif
}

TEST(Logging, LogFileIsNotInheritedByChildProcesses) {
#if defined(NGLOG_OS_WINDOWS) && defined(HAVE_SUBPROCESS)
  using namespace std::chrono_literals;
  // The test does not measure time, so the bound only needs to end a hang
  // before the CTest timeout does.
  constexpr auto kChildTimeout = 20s;

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

  const WindowsHandle process_handle =
      SpawnSelf("--nglog-hold-log-lock \"" + dest + "\" \"" + ready + '"');
  ASSERT_TRUE(process_handle);
  ASSERT_TRUE(WaitForFile(ready));

  SetLogDestination(NGLOG_WARNING, dest.c_str());
  ScopedStreamCapture capture{StandardStream::kStderr};
  LOG(WARNING) << "message that cannot be written while the file is locked";
  const std::string stderr_output = capture.Stop();
  const std::string expected_error = nglog::tools::TrimTrailingCRLF(
      nglog::tools::FormatWindowsMessage(ERROR_LOCK_VIOLATION));
  ASSERT_FALSE(expected_error.empty());
  EXPECT_THAT(stderr_output, AllOf(HasSubstr("Could not create log file '"),
                                   HasSubstr(dest), HasSubstr(expected_error)));

  EXPECT_TRUE(TerminateProcess(process_handle.get(), 0));
  EXPECT_EQ(WaitForSingleObject(process_handle.get(),
                                kChildProcessTimeoutMilliseconds),
            WAIT_OBJECT_0);
  EXPECT_TRUE(WaitForLogFileUnlock(dest));

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
    // A shell link starts with its header size as a little-endian integer.
    constexpr unsigned char kShellLinkHeaderSize = 0x4c;
    EXPECT_THAT(header, ElementsAre(kShellLinkHeaderSize, 0, 0, 0));
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
  EXPECT_THAT(ReadLogFile(sym), HasSubstr("message to new symlink"));

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
  EXPECT_THAT(ReadLogFile(dest), HasSubstr("message to new extension"));

  EXPECT_THAT(GetFiles(dest + "*"), ElementsAre(EndsWith("specialextension")));

  // Release file handle for the destination file to unlock the file in Windows.
  LogToStderr();
  // Log files of later tests must not inherit the extension.
  SetLogFilenameExtension("");
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

TEST(Logging, DisablingAnInactiveCleanerKeepsOverdueLogs) {
#if defined(HAVE_UNISTD_H) && !defined(NGLOG_OS_WINDOWS)
  auto save = nglog::MakeScopedFlags(
      nglog::MakeScopedFlag(FLAGS_logtostderr, false),
      nglog::MakeScopedFlag(FLAGS_timestamp_in_logfile_name, true));

  const std::string dest = TestTmpDir() + "/logging_test_inactive_cleaner";
  DeleteFiles(dest + "*");
  SetLogDestination(NGLOG_INFO, dest.c_str());
  LOG(INFO) << "message that registers the log file with the cleaner";
  FlushLogFiles(NGLOG_INFO);
  LogToStderr();

  const std::string stale_path =
      tools::MakeLogFilename(dest, "20200101-000000.1", std::string(), true);
  {
    std::ofstream stale{stale_path};
  }
  // Older than any overdue period a previous test may have left behind.
  constexpr std::time_t kStaleAgeSeconds = 30 * 24 * 60 * 60;
  struct utimbuf times;
  times.actime = times.modtime = std::time(nullptr) - kStaleAgeSeconds;
  ASSERT_EQ(utime(stale_path.c_str(), &times), 0) << strerror(errno);

  DisableLogCleaner();

  EXPECT_EQ(access(stale_path.c_str(), F_OK), 0);
  DeleteFiles(dest + "*");
#else
  GTEST_SKIP() << "requires utime()";
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

#if !defined(NGLOG_OS_WINDOWS) && defined(HAVE_UNISTD_H)
// A callback may leave errno set while it succeeds, for example after a stat()
// of an entry that disappeared meanwhile.
TEST(PosixPathOperations, DirectoryEnumerationIgnoresErrnoOfCallbacks) {
  const std::string directory =
      TestTmpDir() + "/logging_test_enumerated_directory";
  constexpr mode_t kDirectoryMode = 0700;
  ASSERT_EQ(mkdir(directory.c_str(), kDirectoryMode), 0) << strerror(errno);
  for (const char* name : {"/first", "/second"}) {
    std::ofstream{directory + name};
  }

  int visited = 0;
  const bool enumerated = internal::ForEachPosixDirectory(
      directory,
      [](const char*, std::size_t, void* context) {
        ++*static_cast<int*>(context);
        errno = ENOENT;
        return true;
      },
      &visited);

  EXPECT_TRUE(enumerated);
  EXPECT_EQ(visited, 2);
  DeleteFiles(directory + "/*");
  rmdir(directory.c_str());
}
#endif

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
             const char* message, std::size_t length) override {
    data.append(message, length);
  }

  void Flush() override {}

  std::uint32_t LogSize() override {
    return static_cast<std::uint32_t>(data.length());
  }

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
  EXPECT_THAT(my_logger->data, HasSubstr("Send to wrapped logger"));
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

  // A sink that changes errno stands in for any failing system call on the
  // logging path.
  class ErrnoChangingSink : public LogSink {
   public:
    void send(LogSeverity, const char*, const char*, int, const LogMessageTime&,
              const char*, std::size_t) override {
      errno = EBADF;
    }
  } sink;
  AddLogSink(&sink);
  errno = ENOENT;
  LOG(INFO) << "foo";
  const int logged_errno = errno;
  RemoveLogSink(&sink);
  EXPECT_EQ(logged_errno, ENOENT);
}

// Repeats `pattern` until the result is `length` characters long.
static string RepeatToLength(const string& pattern, std::size_t length) {
  string result;
  result.reserve(length + pattern.size());
  while (result.size() < length) {
    result += pattern;
  }
  result.resize(length);
  return result;
}

static void TestOneTruncate(const char* path, std::uint64_t limit,
                            std::uint64_t keep, std::size_t dsize,
                            std::size_t ksize, std::size_t expect) {
  SCOPED_TRACE(path);
  FileDescriptor fd{open(path, O_RDWR | O_CREAT | O_TRUNC, 0600)};
  ASSERT_TRUE(fd) << strerror(errno);

  // Fill the file with the requested data; first discard data, then kept data
  const string contents =
      RepeatToLength("DISCARDME!", dsize) + RepeatToLength("KEEPME!", ksize);
  ASSERT_EQ(write(fd.get(), contents.data(), contents.size()),
            static_cast<ssize_t>(contents.size()))
      << strerror(errno);

  TruncateLogFile(path, limit, keep);

  // File should now be shorter
  struct stat statbuf;
  ASSERT_EQ(fstat(fd.get(), &statbuf), 0) << strerror(errno);
  ASSERT_EQ(static_cast<std::size_t>(statbuf.st_size), expect);
  ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0) << strerror(errno);

  // File should contain the suffix of the original file
  string truncated(expect, '\0');
  ASSERT_EQ(read(fd.get(), &truncated[0], truncated.size()),
            static_cast<ssize_t>(expect))
      << strerror(errno);
  EXPECT_EQ(truncated, contents.substr(contents.size() - expect));
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

  EXPECT_EQ(ReadFileContents(path), kKept);
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
  ASSERT_EQ(symlink(path.c_str(), linkname.c_str()), 0) << strerror(errno);
  TestOneTruncate(linkname.c_str(), 10, 10, 0, 30, 30);
#  endif

  // The /proc/self path makes sense only for linux.
#  if defined(NGLOG_OS_LINUX)
  // Through an open fd symlink should work
  const FileDescriptor fd{open(path.c_str(), O_APPEND | O_WRONLY)};
  ASSERT_TRUE(fd) << strerror(errno);
  const string fdpath = "/proc/self/fd/" + std::to_string(fd.get());
  TestOneTruncate(fdpath.c_str(), 10, 10, 10, 10, 10);
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

  ScopedStreamCapture capture{StandardStream::kStdout};
  const string message(2U << 20U, 'x');
  ASSERT_EQ(fwrite(message.data(), message.size(), 1, stdout), 1U);
  ASSERT_EQ(fflush(stdout), 0);
  // Buffered output must be part of the kept data.
  ASSERT_GE(fputs(kPending, stdout), 0);

  TruncateStdoutStderr();
  // Output after the truncation must continue at the new end of the file.
  ASSERT_GE(fputs(kTail, stdout), 0);
  ASSERT_EQ(fflush(stdout), 0);
  const string output = capture.Stop();

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
  nglog::SetFlag(FLAGS_drop_log_memory, true);
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
  std::istringstream contents{ReadLogFile(dest)};
  int line_count = 0;
  for (std::string line; std::getline(contents, line);) {
    if (line.find(filler) != std::string::npos) {
      ++line_count;
    }
  }
  EXPECT_EQ(line_count, kThreads * kMessagesPerThread);

  // Release file handle for the destination file to unlock the file in
  // Windows.
  LogToStderr();
  DeleteFiles(dest + "*");
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

// Data races here only fail the test under ThreadSanitizer.
TEST(Logging, DefaultLoggerQueriesDoNotRaceWithLogging) {
  auto save = nglog::MakeScopedFlags(
      nglog::MakeScopedFlag(FLAGS_logtostderr, false),
      nglog::MakeScopedFlag(FLAGS_logtostdout, false),
      nglog::MakeScopedFlag(FLAGS_timestamp_in_logfile_name, false));

  const std::string destination =
      TestTmpDir() + "/logging_test_default_logger_queries";
  DeleteFiles(destination + "*");
  SetLogDestination(NGLOG_INFO, destination.c_str());

  constexpr int kIterations = 1000;
  std::atomic<bool> done{false};
  std::thread querier{[&done] {
    base::Logger* const logger = base::GetLogger(NGLOG_INFO);
    while (!done.load(std::memory_order_acquire)) {
      logger->Flush();
      static_cast<void>(logger->LogSize());
    }
  }};
  for (int i = 0; i < kIterations; ++i) {
    LOG(INFO) << "message logged while the logger is queried";
  }
  done.store(true, std::memory_order_release);
  querier.join();

  LogToStderr();
  DeleteFiles(destination + "*");
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
  EXPECT_EQ(metrics.acquisitions, 1U);
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
             const char* message, std::size_t length) override {
    wrapped_logger_->Write(force_flush, timestamp, message, length);
  }
  void Flush() override { wrapped_logger_->Flush(); }
  std::uint32_t LogSize() override { return wrapped_logger_->LogSize(); }

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
  RestoreTestPrefixFormatter();
  EXPECT_TRUE(IsLoggingInitialized());
}

// A data race here only fails the test under ThreadSanitizer, which makes the
// child exit with a nonzero status.
TEST(Logging, ShutdownDoesNotRaceWithLogCleaner) {
#if defined(GTEST_HAS_DEATH_TEST)
  using namespace std::chrono_literals;
  ASSERT_EXIT(
      {
        constexpr int kRounds = 20;
        nglog::SetFlag(FLAGS_logtostderr, false);
        nglog::SetFlag(FLAGS_logtostdout, false);
        nglog::SetFlag(FLAGS_log_dir, TestTmpDir());
        EnableLogCleaner(0h);
        for (int i = 0; i < kRounds; ++i) {
          LOG(INFO) << "message that makes the cleaner scan";
          ShutdownLogging();
          InitializeLogging(g_argv0);
        }
        DisableLogCleaner();
        std::exit(EXIT_SUCCESS);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
#endif
}

TEST(Logging, LogPeriodically) {
  using namespace std::chrono_literals;
  fprintf(stderr, "==== Test log periodically\n");

  constexpr std::chrono::nanoseconds kPeriod = 10ms;
  constexpr double kPeriodSeconds =
      std::chrono::duration<double>(kPeriod).count();
  // Converting the period to seconds and back may round it down.
  constexpr std::chrono::nanoseconds kRoundingTolerance = 1us;
  constexpr int kMessageCount = 10;

  int logged = 0;
  const auto start = std::chrono::steady_clock::now();
  while (logged < kMessageCount) {
    LOG_EVERY_T(INFO, kPeriodSeconds) << "Timed Message #" << logged++;
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;

  // Consecutive messages are more than one period apart. The total duration
  // is measured around the loop rather than between messages, so that a
  // stall between the logging decision and a timestamp cannot shorten it. A
  // stall only lengthens the duration, so there is no upper bound, and a
  // macro that never logs again hangs until the CTest timeout.
  constexpr std::chrono::nanoseconds kMinimumElapsed =
      (kMessageCount - 1) * (kPeriod - kRoundingTolerance);
  EXPECT_GE(
      std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count(),
      kMinimumElapsed.count());
}

namespace nglog {
inline namespace tools {
// in logging.cc
extern bool SafeFNMatch_(const char* pattern, std::size_t patt_len,
                         const char* str, std::size_t str_len);
}  // namespace tools
}  // namespace nglog

static bool WrapSafeFNMatch(string pattern, string str) {
  pattern += "abc";
  str += "defgh";
  return SafeFNMatch_(pattern.data(), pattern.size() - 3, str.data(),
                      str.size() - 5);
}

TEST(SafeFNMatch, logging) {
  EXPECT_TRUE(WrapSafeFNMatch("foo", "foo"));
  EXPECT_FALSE(WrapSafeFNMatch("foo", "bar"));
  EXPECT_FALSE(WrapSafeFNMatch("foo", "fo"));
  EXPECT_FALSE(WrapSafeFNMatch("foo", "foo2"));
  EXPECT_TRUE(WrapSafeFNMatch("bar/foo.ext", "bar/foo.ext"));
  EXPECT_TRUE(WrapSafeFNMatch("*ba*r/fo*o.ext*", "bar/foo.ext"));
  EXPECT_FALSE(WrapSafeFNMatch("bar/foo.ext", "bar/baz.ext"));
  EXPECT_FALSE(WrapSafeFNMatch("bar/foo.ext", "bar/foo"));
  EXPECT_FALSE(WrapSafeFNMatch("bar/foo.ext", "bar/foo.ext.zip"));
  EXPECT_TRUE(WrapSafeFNMatch("ba?/*.ext", "bar/foo.ext"));
  EXPECT_TRUE(WrapSafeFNMatch("ba?/*.ext", "baZ/FOO.ext"));
  EXPECT_FALSE(WrapSafeFNMatch("ba?/*.ext", "barr/foo.ext"));
  EXPECT_FALSE(WrapSafeFNMatch("ba?/*.ext", "bar/foo.ext2"));
  EXPECT_TRUE(WrapSafeFNMatch("ba?/*", "bar/foo.ext2"));
  EXPECT_TRUE(WrapSafeFNMatch("ba?/*", "bar/"));
  EXPECT_FALSE(WrapSafeFNMatch("ba?/?", "bar/"));
  EXPECT_FALSE(WrapSafeFNMatch("ba?/*", "bar"));
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
    {
      std::lock_guard<std::mutex> lock{mutex_};
      RAW_LOG(INFO, "Buffering");
      messages_.push(message);
    }
    RAW_LOG(INFO, "Buffered");
  }

  // Wait for the buffer to clear (can't use LOG() here).
  void Wait() {
    RAW_LOG(INFO, "Waiting");
    std::unique_lock<std::mutex> lock{mutex_};
    waiting_ = true;
    condition_.notify_all();
    condition_.wait(lock, [this] { return messages_.empty(); });
    waiting_ = false;
    RAW_LOG(INFO, "Waited");
  }

  // Trigger thread exit.
  void Stop() {
    {
      std::lock_guard<std::mutex> lock{mutex_};
      should_exit_ = true;
    }
    condition_.notify_all();
  }

  void Join() { t_.join(); }

 private:
  // Thread body; CAN use LOG() here!
  void Run() {
    std::unique_lock<std::mutex> lock{mutex_};
    while (true) {
      // A message is processed only once the logging thread waits for it.
      // This orders the output of both threads deterministically regardless
      // of how either thread is scheduled.
      condition_.wait(lock, [this] {
        return should_exit_ || (waiting_ && !messages_.empty());
      });
      if (messages_.empty()) {
        break;
      }
      RAW_LOG(INFO, "Sink got a messages");  // only RAW_LOG under mutex_ here
      const string message = messages_.front();
      // Normally this would be some more real/involved logging logic
      // where LOG() usage can't be eliminated,
      // e.g. pushing the message over with an RPC:
      const std::size_t messages_left = messages_.size() - 1;
      lock.unlock();
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
      lock.lock();
      messages_.pop();
      condition_.notify_all();
    }
  }

  std::mutex mutex_;
  std::condition_variable condition_;
  bool should_exit_{false};
  bool waiting_{false};
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
            std::size_t message_len) override {
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
  EXPECT_THAT(global_messages, SizeIs(3));
}

TEST(Strerror, logging) {
  int errcode = EINTR;
  std::string msg = strerror(errcode);
  const std::size_t buf_size = msg.size() + 1;
  std::vector<char> buf(buf_size);
  EXPECT_EQ(posix_strerror_r(errcode, nullptr, 0), -1);
  buf[0] = 'A';
  EXPECT_EQ(posix_strerror_r(errcode, buf.data(), 0), -1);
  EXPECT_EQ(buf[0], 'A');
  EXPECT_EQ(posix_strerror_r(errcode, nullptr, buf_size), -1);
#if defined(NGLOG_OS_MACOSX) || defined(NGLOG_OS_FREEBSD) || \
    defined(NGLOG_OS_OPENBSD)
  // MacOSX or FreeBSD considers this case is an error since there is
  // no enough space.
  EXPECT_EQ(posix_strerror_r(errcode, buf.data(), 1), -1);
#else
  EXPECT_EQ(posix_strerror_r(errcode, buf.data(), 1), 0);
#endif
  EXPECT_STREQ(buf.data(), "");
  EXPECT_EQ(posix_strerror_r(errcode, buf.data(), buf_size), 0);
  EXPECT_STREQ(buf.data(), msg.c_str());
  EXPECT_EQ(msg, StrError(errcode));
}

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
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_minloglevel, static_cast<std::int32_t>(NGLOG_ERROR));
  testing::MockFunction<int()> disabled_log_argument;
  EXPECT_CALL(disabled_log_argument, Call()).Times(0);

  LOG_IF(INFO, true) << disabled_log_argument.AsStdFunction()();
}

TEST(Logging, FatalIgnoresMinimumLogLevel) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_minloglevel, NGLOG_FATAL + 1);
  const ScopedThrowOnFatal throw_on_fatal;

  EXPECT_THROW({ LOG(FATAL) << "fatal log"; }, std::logic_error);
  const auto fatal_check = [] { CHECK(false) << "fatal check"; };
  EXPECT_THROW(fatal_check(), std::logic_error);
}

// LOG(FATAL) does not return, so it may end a [[noreturn]] function. Clang
// rejects the function with -Winvalid-noreturn otherwise.
[[noreturn]] void LogFatal() { LOG(FATAL) << "fatal log"; }

TEST(Logging, FatalEndsNoreturnFunction) {
  const ScopedThrowOnFatal throw_on_fatal;

  EXPECT_THROW(LogFatal(), std::logic_error);
}

TEST(Logging, DefaultPrefixFormatting) {
  nglog::ScopedFlags<> save;
  RouteLogMessagesTo(StandardStream::kStderr);
  InstallPrefixFormatter(nullptr);

  ScopedStreamCapture capture{StandardStream::kStderr};
  const int line = __LINE__ + 1;
  LOG(WARNING) << "default prefix formatting";
  const std::string output = capture.Stop();

  RestoreTestPrefixFormatter();

  EXPECT_THAT(SplitLines(output),
              ElementsAre(MatchesRegex(DefaultPrefixPattern('W', line) +
                                       "default prefix formatting")));
}

TEST(RawLogging, PrefixFormatting) {
  nglog::ScopedFlags<> save;
  RouteLogMessagesTo(StandardStream::kStderr);

  ScopedStreamCapture capture{StandardStream::kStderr};
  const int line = __LINE__ + 1;
  RAW_LOG(ERROR, "raw prefix formatting");
  const std::string output = capture.Stop();

  // RAW_LOG cannot convert the time without allocating memory and therefore
  // writes zeros in place of the date and time.
  EXPECT_THAT(SplitLines(output),
              ElementsAre(MatchesRegex(
                  "E00000000 00:00:00\\.000000 +" + CurrentThreadId() + ' ' +
                  kSourceBasenamePattern + ':' + std::to_string(line) +
                  "\\] RAW: raw prefix formatting")));
}

// A message that fills the buffer exactly is either written in full and ends
// its line or replaced by the error, instead of losing its last character or
// the newline that separates it from the next one.
TEST(RawLogging, MessageFillingBufferEndsLine) {
  nglog::ScopedFlags<> save;
  RouteLogMessagesTo(StandardStream::kStderr);
  // Size of the buffer RAW_LOG formats a line into.
  constexpr std::size_t kBufferSize = 3000;
  constexpr std::size_t kMaxSlack = 4;
  const auto log = [](const std::string& message) {
    ScopedStreamCapture capture{StandardStream::kStderr};
    RAW_LOG(ERROR, "%s", message.c_str());
    return capture.Stop();
  };
  // The prefix is as long for every message logged at the same line.
  const std::size_t prefix_size = log("").size() - 1;

  // A message fits only if the newline and the terminating null character fit
  // after it as well.
  constexpr std::size_t kNewlineAndNull = 2;
  for (std::size_t slack = 0; slack != kMaxSlack; ++slack) {
    const std::string message(kBufferSize - prefix_size - slack, 'x');
    if (slack < kNewlineAndNull) {
      EXPECT_THAT(log(message),
                  EndsWith("RAW_LOG ERROR: The Message was too long!\n"))
          << "slack " << slack;
    } else {
      EXPECT_THAT(log(message), EndsWith(message + '\n')) << "slack " << slack;
    }
  }
}

//! Groups the digits of numbers in threes like many locales do.
class GroupingPunctuation : public std::numpunct<char> {
 protected:
  char do_thousands_sep() const override { return ','; }
  std::string do_grouping() const override { return "\3"; }
};

//! Installs a global locale that groups digits and restores the previous one.
class ScopedGroupingLocale {
 public:
  ScopedGroupingLocale()
      : previous_{std::locale::global(
            std::locale{std::locale::classic(), new GroupingPunctuation})} {}
  ~ScopedGroupingLocale() { std::locale::global(previous_); }

  ScopedGroupingLocale(const ScopedGroupingLocale&) = delete;
  ScopedGroupingLocale& operator=(const ScopedGroupingLocale&) = delete;

 private:
  std::locale previous_;
};

// RAW_LOG writes the thread identifier like the classic locale writes it,
// whatever the global locale. A stream would use the global locale, whose
// construction also acquires a lock if it is not the classic one.
TEST(RawLogging, PrefixIgnoresGlobalLocale) {
  nglog::ScopedFlags<> save;
  RouteLogMessagesTo(StandardStream::kStderr);
  const std::string thread_id = CurrentThreadId();

  ScopedStreamCapture capture{StandardStream::kStderr};
  {
    const ScopedGroupingLocale locale;
    RAW_LOG(ERROR, "raw prefix formatting");
  }
  const std::string output = capture.Stop();

  EXPECT_THAT(SplitLines(output),
              ElementsAre(HasSubstr(" " + thread_id + ' ')));
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

// Recurses `depth` frames before capturing the stack trace.
NGLOG_ATTRIBUTE_NOINLINE static std::string StackTraceAtDepth(int depth) {
  if (depth > 0) {
    std::string trace = StackTraceAtDepth(depth - 1);
    NGLOG_BLOCK_TAIL_CALL_OPTIMIZATION();
    return trace;
  }
  return GetStackTrace();
}

TEST(GetStackTrace, SaysWhenMoreFramesFollow) {
  if (!UnwinderWalksPastMaxStackDumpFrames()) {
    GTEST_SKIP() << "the unwinder stops before kMaxStackDumpFrames";
  }
  EXPECT_THAT(StackTraceAtDepth(kDepthPastStackDumpLimit),
              HasCutOffStackDump(kMaxStackDumpFrames));
}

// Assumes the frames below the test body, gtest's and main's, stay under
// kMaxStackDumpFrames.
TEST(GetStackTrace, ShallowTraceIsNotMarkedAsCutOff) {
  EXPECT_THAT(StackTraceAtDepth(0), Not(HasSubstr(kStackDumpCutOffMarker)));
}

#  ifdef GTEST_HAS_DEATH_TEST
// Recurses `depth` frames before the fatal LOG. A negative depth returns
// without logging, which gives the compiler a path that does not recurse.
NGLOG_ATTRIBUTE_NOINLINE static void FatalAtDepth(int depth) {
  if (depth > 0) {
    FatalAtDepth(depth - 1);
    NGLOG_BLOCK_TAIL_CALL_OPTIMIZATION();
    return;
  }
  LOG_IF(FATAL, depth == 0) << "fatal at depth";
}

TEST(LogFatalStackTrace, SaysWhenMoreFramesFollow) {
  if (!UnwinderWalksPastMaxStackDumpFrames()) {
    GTEST_SKIP() << "the unwinder stops before kMaxStackDumpFrames";
  }
  ASSERT_DEATH(FatalAtDepth(kDepthPastStackDumpLimit),
               HasCutOffStackDump(kMaxStackDumpFrames));
}

#    ifdef HAVE_SYMBOLIZE
//! Precedes everything that WriteTaggedToStderr() receives.
constexpr char kFailureWriterTag[] = "writer>";

// Tags what the failure writer receives, so that it can be told apart from
// output written to stderr directly.
static void WriteTaggedToStderr(const char* data, std::size_t size) {
  std::fprintf(stderr, "%s%.*s", kFailureWriterTag, static_cast<int>(size),
               data);
}

// Returns the lines of `output` that WriteTaggedToStderr() started, without
// the tags.
static std::string FailureWriterOutput(const std::string& output) {
  constexpr std::size_t kTagLength = sizeof(kFailureWriterTag) - 1;
  std::string written;
  std::istringstream lines{output};
  for (std::string line; std::getline(lines, line);) {
    if (line.compare(0, kTagLength, kFailureWriterTag) != 0) {
      continue;
    }
    for (std::string::size_type tag = line.find(kFailureWriterTag);
         tag != std::string::npos; tag = line.find(kFailureWriterTag, tag)) {
      line.erase(tag, kTagLength);
    }
    written += line;
    written += '\n';
  }
  return written;
}

// With symbolization on, the frames go through the failure writer, so the
// marker must as well.
TEST(LogFatalStackTrace, MarkerReachesInstalledFailureWriter) {
  if (!UnwinderWalksPastMaxStackDumpFrames()) {
    GTEST_SKIP() << "the unwinder stops before kMaxStackDumpFrames";
  }
  ASSERT_DEATH(
      {
        InstallFailureWriter(&WriteTaggedToStderr);
        FatalAtDepth(kDepthPastStackDumpLimit);
      },
      ResultOf(&FailureWriterOutput, HasCutOffStackDump(kMaxStackDumpFrames)));
}

// With symbolization off, the frames bypass the failure writer, so the marker
// must as well.
TEST(LogFatalStackTrace, MarkerFollowsUnsymbolizedFramesToStderr) {
  if (!UnwinderWalksPastMaxStackDumpFrames()) {
    GTEST_SKIP() << "the unwinder stops before kMaxStackDumpFrames";
  }
  ASSERT_DEATH(
      {
        nglog::SetFlag(FLAGS_symbolize_stacktrace, false);
        InstallFailureWriter(&WriteTaggedToStderr);
        FatalAtDepth(kDepthPastStackDumpLimit);
      },
      HasCutOffStackDump(kMaxStackDumpFrames));
}
#    endif  // HAVE_SYMBOLIZE
#  endif    // GTEST_HAS_DEATH_TEST

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
  EXPECT_THAT(buf, ElementsAre(HasSubstr("OK")));

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
  EXPECT_GE(gmtoff, utc_min_offset);
  EXPECT_LE(gmtoff, utc_max_offset);
}

// Emscripten derives the time zone from the JavaScript host and ignores TZ.
#if !defined(NGLOG_OS_WINDOWS) && !defined(NGLOG_OS_EMSCRIPTEN)
TEST(LogMsgTime, gmtoffSubHour) {
  // The gmtoffset() API is documented to return seconds, so time zones whose
  // offset is not a whole number of hours (e.g. India at UTC+05:30) must be
  // reported without truncating the minutes.
  using namespace std::chrono_literals;
  std::chrono::seconds gmtoff;
  {
    // POSIX TZ format: the offset is the value added to local time to obtain
    // UTC, so "IST-5:30" describes a fixed zone 5 h 30 min east of UTC.
    const ScopedEnvironmentVariable tz{"TZ", "IST-5:30"};
    tzset();
    gmtoff = nglog::LogMessage(__FILE__, __LINE__).time().gmtoffset();
  }
  tzset();

  EXPECT_EQ(gmtoff, 5h + 30min);
}
#endif  // !defined(NGLOG_OS_WINDOWS) && !defined(NGLOG_OS_EMSCRIPTEN)

// Emscripten derives the time zone from the JavaScript host and ignores TZ.
#ifndef NGLOG_OS_EMSCRIPTEN
TEST(LogMsgTime, gmtoffIsZeroForUtcTime) {
  using namespace std::chrono_literals;
  auto save =
      nglog::MakeScopedFlags(nglog::MakeScopedFlag(FLAGS_log_utc_time, true));
  std::chrono::seconds gmtoff;
  {
    // A local time zone away from UTC must not leak into UTC timestamps.
    const ScopedEnvironmentVariable tz{"TZ", "IST-5:30"};
    tzset();
    gmtoff = nglog::LogMessage(__FILE__, __LINE__).time().gmtoffset();
  }
  tzset();

  EXPECT_EQ(gmtoff, 0s);
}
#endif  // !defined(NGLOG_OS_EMSCRIPTEN)

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
  const ScopedThrowOnFatal throw_on_fatal;
  EXPECT_THROW({ LOG(FATAL) << "must throw to fail"; }, std::logic_error);
}

TEST(LoggingBeforeInitialization, WritesAllMessagesToStderr) {
  const std::vector<LineMatcher> expected{
      LogLine(
          'I',
          "RAW: Test: v=0 stderrthreshold=2 logtostderr=0 alsologtostderr=0"),
      LogLine('I', "RAW: vlog -1"),
      LogLine('I', "RAW: vlog 0"),
      LogLine('I', "RAW: log info"),
      LogLine('W', "RAW: log warning"),
      LogLine('E', "RAW: log error"),
      StrEq("WARNING: Logging before InitializeLogging() is written to STDERR"),
      LogLine('I', "vlog -1"),
      LogLine('I', "vlog 0"),
      LogLine('I', "log info"),
      LogLine('W', "log warning"),
      LogLine('E', "log error"),
      LogLine('I', "vlog_if -1"),
      LogLine('I', "vlog_if 0"),
      LogLine('I', "log_if info"),
      LogLine('W', "log_if warning"),
      LogLine('E', "log_if error"),
      LogLine('I', "vlog_if 0 expr"),
      LogLine('I', "log_if info expr"),
      LogLine('E', "log_if error expr"),
      LogLine('I', "log_if info every 1 expr"),
      LogLine('E', "log_if error every 1 expr"),
      LogLine('I', "vlog_if 0 every 1 expr"),
      LogLine(
          'I',
          "RAW: Test: v=0 stderrthreshold=0 logtostderr=0 alsologtostderr=0"),
      LogLine('I', "RAW: vlog -1"),
      LogLine('I', "RAW: vlog 0"),
      LogLine('I', "RAW: log info"),
      LogLine('W', "RAW: log warning"),
      LogLine('E', "RAW: log error"),
      LogLine('I', "vlog -1"),
      LogLine('I', "vlog 0"),
      LogLine('I', "log info"),
      LogLine('W', "log warning"),
      LogLine('E', "log error"),
      LogLine('I', "vlog_if -1"),
      LogLine('I', "vlog_if 0"),
      LogLine('I', "log_if info"),
      LogLine('W', "log_if warning"),
      LogLine('E', "log_if error"),
      LogLine('I', "vlog_if 0 expr"),
      LogLine('I', "log_if info expr"),
      LogLine('E', "log_if error expr"),
      LogLine('I', "log_if info every 1 expr"),
      LogLine('E', "log_if error every 1 expr"),
      LogLine('I', "vlog_if 0 every 1 expr"),
  };
  EXPECT_THAT(SplitLines(early_stderr), ElementsAreArray(expected));
}

TEST(LogMacros, WriteFormattedMessagesInOrder) {
  nglog::ScopedFlags<> save;
  RouteLogMessagesTo(StandardStream::kStderr);

  // The expected messages of the EVERY_N macros assume that their counters
  // start from zero, which holds because no other test calls TestLogging().
  ScopedStreamCapture capture{StandardStream::kStderr};
  TestLogging(true);
  const std::vector<std::string> lines = SplitLines(capture.Stop());

  // PLOG appends the description of errno, which differs between platforms,
  // followed by the error number.
  const std::vector<LineMatcher> expected{
      LogLine('I', "foo bar 10 3.4"),
      LogLine('E', "Plog every 2, iteration 1: " + StrError(0) + " [0]"),
      LogLine('E', "Log every 3, iteration 1"),
      LogLine('E', "Log every 4, iteration 1"),
      LogLine('W', "Log if every 5, iteration 1"),
      LogLine('I', "Log if every 1, iteration 1"),
      LogLine('E', "Log if less than 3 every 2, iteration 1"),
      LogLine('I', "Log if every 1, iteration 2"),
      LogLine('E', "Plog every 2, iteration 3: " + StrError(2) + " [2]"),
      LogLine('I', "Log if every 1, iteration 3"),
      LogLine('E', "Log if less than 3 every 2, iteration 3"),
      LogLine('E', "Log every 3, iteration 4"),
      LogLine('I', "Log if every 1, iteration 4"),
      LogLine('E', "Plog every 2, iteration 5: " + StrError(4) + " [4]"),
      LogLine('E', "Log every 4, iteration 5"),
      LogLine('I', "Log if every 1, iteration 5"),
      LogLine('W', "Log if every 5, iteration 6"),
      LogLine('I', "Log if every 1, iteration 6"),
      LogLine('E', "Plog every 2, iteration 7: " + StrError(6) + " [6]"),
      LogLine('E', "Log every 3, iteration 7"),
      LogLine('I', "Log if every 1, iteration 7"),
      LogLine('I', "Log if every 1, iteration 8"),
      LogLine('E', "Plog every 2, iteration 9: " + StrError(8) + " [8]"),
      LogLine('E', "Log every 4, iteration 9"),
      LogLine('I', "Log if every 1, iteration 9"),
      LogLine('E', "Log every 3, iteration 10"),
      LogLine('I', "Log if every 1, iteration 10"),
      LogLine('W', "log_if this"),
      LogLine('I', "array"),
      LogLine('I', "const array"),
      LogLine('E', "foo 1000       1000 3e8"),
      LogLine('I', "foo          1"),
      LogLine('E', "inner"),
      LogLine('E', "outer"),
      StrEq("no prefix"),
  };
  EXPECT_THAT(lines, ElementsAreArray(expected));
}

//! Standard stream that log messages are routed to.
struct StreamRoute {
  const char* name;
  StandardStream stream;
};

void PrintTo(const StreamRoute& route, std::ostream* os) { *os << route.name; }

/**
 * Lines written to the stream that log messages are routed to and to the
 * other standard stream.
 */
struct RoutedOutput {
  std::vector<std::string> routed;
  std::vector<std::string> other;
};

class LoggingToStream : public testing::TestWithParam<StreamRoute> {
 protected:
  void SetUp() override { RouteLogMessagesTo(GetParam().stream); }

  bool RoutedToStderr() const {
    return GetParam().stream == StandardStream::kStderr;
  }

  //! Captures what `function` writes to both standard streams.
  template <class Function>
  RoutedOutput Capture(Function function) const {
    const StandardStream other =
        RoutedToStderr() ? StandardStream::kStdout : StandardStream::kStderr;
    ScopedStreamCapture other_capture{other};
    ScopedStreamCapture routed_capture{GetParam().stream};
    function();
    RoutedOutput output;
    output.routed = SplitLines(routed_capture.Stop());
    output.other = SplitLines(other_capture.Stop());
    return output;
  }

 private:
  nglog::ScopedFlags<> flags_;
};

INSTANTIATE_TEST_SUITE_P(
    Streams, LoggingToStream,
    testing::Values(StreamRoute{"Stderr", StandardStream::kStderr},
                    StreamRoute{"Stdout", StandardStream::kStdout}),
    CaseName{});

TEST_P(LoggingToStream, RawLoggingWritesOnlyToStderr) {
  const RoutedOutput output = Capture(&TestRawLogging);

  const std::vector<LineMatcher> expected{
      LogLine('I', "RAW: foo bar 10 3.400000"),
      LogLine('W', "RAW: array"),
      LogLine('I', "RAW: const array"),
      LogLine('I', "RAW: ptr " + FormatPointer(reinterpret_cast<void*>(
                                     kRawLogPointerValue))),
      LogLine('I', "RAW: ptr " + FormatPointer(nullptr)),
      LogLine('E', "RAW: foo 1000 0000001000 3e8"),
      LogLine('I', "RAW: foo 1000"),
      LogLine('I', "RAW: foo 1000"),
      LogLine('W', "RAW: RAW_LOG ERROR: The Message was too long!"),
      LogLine('I', "RAW: RAW_LOG ERROR: The Message was too long!"),
      LogLine('I', "RAW: log"),
      LogLine('I', "RAW: vlog 0 on"),
      LogLine('I', "RAW: log"),
      LogLine('I', "RAW: vlog 1 on"),
      LogLine('I', "RAW: vlog 2 on"),
  };
  if (RoutedToStderr()) {
    EXPECT_THAT(output.routed, ElementsAreArray(expected));
    EXPECT_THAT(output.other, IsEmpty());
  } else {
    EXPECT_THAT(output.routed, IsEmpty());
    EXPECT_THAT(output.other, ElementsAreArray(expected));
  }
}

TEST_P(LoggingToStream, LogStringCollectsOrWritesMessages) {
  const RoutedOutput output = Capture(&TestLogString);

  const std::vector<LineMatcher> expected{
      LogLine('I', "LOG_STRING: reported info"),
      LogLine('W', "LOG_STRING: reported warning"),
      LogLine('E', "LOG_STRING: reported error"),
      LogLine('I', "Captured by LOG_STRING:  LOG_STRING: collected info"),
      LogLine('I', "Captured by LOG_STRING:  LOG_STRING: collected warning"),
      LogLine('I', "Captured by LOG_STRING:  LOG_STRING: collected error"),
  };
  EXPECT_THAT(output.routed, ElementsAreArray(expected));
  EXPECT_THAT(output.other, IsEmpty());
}

TEST_P(LoggingToStream, LogToSinkSendsAndWritesMessages) {
  const RoutedOutput output = Capture(&TestLogSink);

  // The lines following "Captured by LOG_TO_SINK:" are the messages the sink
  // formatted, logged without a prefix of their own.
  const std::vector<LineMatcher> expected{
      LogLine('I', "LOG_TO_SINK: collected info"),
      LogLine('W', "LOG_TO_SINK: collected warning"),
      LogLine('E', "LOG_TO_SINK: collected error"),
      LogLine('I', "LOG_TO_SINK: reported info"),
      LogLine('W', "LOG_TO_SINK: reported warning"),
      LogLine('E', "LOG_TO_SINK: reported error"),
      LogLine('I', "Captured by LOG_TO_SINK:"),
      LogLine('I', "LOG_TO_SINK: collected info"),
      LogLine('W', "LOG_TO_SINK: collected warning"),
      LogLine('E', "LOG_TO_SINK: collected error"),
      LogLine('I', "LOG_TO_SINK_BUT_NOT_TO_LOGFILE: collected info"),
      LogLine('W', "LOG_TO_SINK_BUT_NOT_TO_LOGFILE: collected warning"),
      LogLine('E', "LOG_TO_SINK_BUT_NOT_TO_LOGFILE: collected error"),
  };
  EXPECT_THAT(output.routed, ElementsAreArray(expected));
  EXPECT_THAT(output.other, IsEmpty());
}

TEST_P(LoggingToStream, LogToStringCollectsOrWritesMessages) {
  const RoutedOutput output = Capture(&TestLogToString);

  const std::vector<LineMatcher> expected{
      LogLine('I', "LOG_TO_STRING: collected info"),
      LogLine('I', "Captured by LOG_TO_STRING:  LOG_TO_STRING: collected info"),
      LogLine('W', "LOG_TO_STRING: collected warning"),
      LogLine('I',
              "Captured by LOG_TO_STRING:  LOG_TO_STRING: collected warning"),
      LogLine('E', "LOG_TO_STRING: collected error"),
      LogLine('I',
              "Captured by LOG_TO_STRING:  LOG_TO_STRING: collected error"),
      LogLine('I', "LOG_TO_STRING: reported info"),
      LogLine('W', "LOG_TO_STRING: reported warning"),
      LogLine('E', "LOG_TO_STRING: reported error"),
  };
  EXPECT_THAT(output.routed, ElementsAreArray(expected));
  EXPECT_THAT(output.other, IsEmpty());
}

TEST_P(LoggingToStream, SinkWaitsUntilMessagesAreSent) {
  const RoutedOutput output = Capture(&TestLogSinkWaitTillSent);

  if (RoutedToStderr()) {
    const std::vector<LineMatcher> expected{
        LogLine('I', "Message 1"),
        LogLine('I', "RAW: Buffering"),
        LogLine('I', "RAW: Buffered"),
        LogLine('I', "RAW: Waiting"),
        LogLine('I', "RAW: Sink got a messages"),
        LogLine('I', StartsWithThen("Sink is sending out a message: ",
                                    LogLine('I', "Message 1"))),
        LogLine('I', "Have 0 left"),
        LogLine('I', "RAW: Waited"),
        LogLine('E', "Message 2"),
        LogLine('I', "RAW: Buffering"),
        LogLine('I', "RAW: Buffered"),
        LogLine('I', "RAW: Waiting"),
        LogLine('I', "RAW: Sink got a messages"),
        LogLine('I', StartsWithThen("Sink is sending out a message: ",
                                    LogLine('E', "Message 2"))),
        LogLine('I', "Have 0 left"),
        LogLine('I', "RAW: Waited"),
        LogLine('W', "Message 3"),
        LogLine('I', "RAW: Buffering"),
        LogLine('I', "RAW: Buffered"),
        LogLine('I', "RAW: Waiting"),
        LogLine('I', "RAW: Sink got a messages"),
        LogLine('I', StartsWithThen("Sink is sending out a message: ",
                                    LogLine('W', "Message 3"))),
        LogLine('I', "Have 0 left"),
        LogLine('I', "RAW: Waited"),
        LogLine('I',
                StartsWithThen("Sink capture: ", LogLine('I', "Message 1"))),
        LogLine('I',
                StartsWithThen("Sink capture: ", LogLine('E', "Message 2"))),
        LogLine('I',
                StartsWithThen("Sink capture: ", LogLine('W', "Message 3"))),
    };
    EXPECT_THAT(output.routed, ElementsAreArray(expected));
    EXPECT_THAT(output.other, IsEmpty());
  } else {
    // RAW_LOG writes to stderr regardless of the stream log messages are
    // routed to.
    const std::vector<LineMatcher> expected{
        LogLine('I', "Message 1"),
        LogLine('I', StartsWithThen("Sink is sending out a message: ",
                                    LogLine('I', "Message 1"))),
        LogLine('I', "Have 0 left"),
        LogLine('E', "Message 2"),
        LogLine('I', StartsWithThen("Sink is sending out a message: ",
                                    LogLine('E', "Message 2"))),
        LogLine('I', "Have 0 left"),
        LogLine('W', "Message 3"),
        LogLine('I', StartsWithThen("Sink is sending out a message: ",
                                    LogLine('W', "Message 3"))),
        LogLine('I', "Have 0 left"),
        LogLine('I',
                StartsWithThen("Sink capture: ", LogLine('I', "Message 1"))),
        LogLine('I',
                StartsWithThen("Sink capture: ", LogLine('E', "Message 2"))),
        LogLine('I',
                StartsWithThen("Sink capture: ", LogLine('W', "Message 3"))),
    };
    const std::vector<LineMatcher> expected_raw{
        LogLine('I', "RAW: Buffering"),
        LogLine('I', "RAW: Buffered"),
        LogLine('I', "RAW: Waiting"),
        LogLine('I', "RAW: Sink got a messages"),
        LogLine('I', "RAW: Waited"),
        LogLine('I', "RAW: Buffering"),
        LogLine('I', "RAW: Buffered"),
        LogLine('I', "RAW: Waiting"),
        LogLine('I', "RAW: Sink got a messages"),
        LogLine('I', "RAW: Waited"),
        LogLine('I', "RAW: Buffering"),
        LogLine('I', "RAW: Buffered"),
        LogLine('I', "RAW: Waiting"),
        LogLine('I', "RAW: Sink got a messages"),
        LogLine('I', "RAW: Waited"),
    };
    EXPECT_THAT(output.routed, ElementsAreArray(expected));
    EXPECT_THAT(output.other, ElementsAreArray(expected_raw));
  }
}

TEST_P(LoggingToStream, ChecksThatHoldWriteNothing) {
  const RoutedOutput output = Capture([] {
    TestCHECK();
    TestDCHECK();
    TestSTREQ();
  });

  EXPECT_THAT(output.routed, IsEmpty());
  EXPECT_THAT(output.other, IsEmpty());
}

//! Flags that LogWithLevels() logs with and the lines it is expected to write.
struct LevelsCase {
  const char* name;
  StandardStream stream;
  std::int32_t verbosity;
  std::int32_t stderr_threshold;
  bool log_to_stderr;
  bool also_log_to_stderr;
  std::vector<ExpectedLine> expected;
};

void PrintTo(const LevelsCase& levels, std::ostream* os) { *os << levels.name; }

class LoggingLevels : public testing::TestWithParam<LevelsCase> {};

/**
 * Each case lists the lines that reach the stream log messages are routed to.
 * LogWithLevels() logs its first line using the flags of the route, which
 * write RAW_LOG messages to stderr. Unless logtostderr or logtostdout is set,
 * the other messages reach stderr only if their severity is at least the
 * stderr threshold or alsologtostderr is set. With logtostdout, messages
 * below the stderr threshold reach stdout and the remaining ones stderr.
 */
const LevelsCase kLevelsCases[] = {
    {"StderrVerbosity0ThresholdInfo",
     StandardStream::kStderr,
     0,
     NGLOG_INFO,
     false,
     false,
     {{'I', "RAW: Test: v=0 stderrthreshold=0 logtostderr=0 alsologtostderr=0"},
      {'I', "RAW: vlog -1"},
      {'I', "RAW: vlog 0"},
      {'I', "RAW: log info"},
      {'W', "RAW: log warning"},
      {'E', "RAW: log error"},
      {'I', "vlog -1"},
      {'I', "vlog 0"},
      {'I', "log info"},
      {'W', "log warning"},
      {'E', "log error"},
      {'I', "vlog_if -1"},
      {'I', "vlog_if 0"},
      {'I', "log_if info"},
      {'W', "log_if warning"},
      {'E', "log_if error"},
      {'I', "vlog_if 0 expr"},
      {'I', "log_if info expr"},
      {'E', "log_if error expr"},
      {'I', "log_if info every 1 expr"},
      {'E', "log_if error every 1 expr"},
      {'I', "vlog_if 0 every 1 expr"}}},
    {"StderrVerbosity1ThresholdInfo",
     StandardStream::kStderr,
     1,
     NGLOG_INFO,
     false,
     false,
     {{'I', "RAW: Test: v=1 stderrthreshold=0 logtostderr=0 alsologtostderr=0"},
      {'I', "RAW: vlog -1"},
      {'I', "RAW: vlog 0"},
      {'I', "RAW: vlog 1"},
      {'I', "RAW: log info"},
      {'W', "RAW: log warning"},
      {'E', "RAW: log error"},
      {'I', "vlog -1"},
      {'I', "vlog 0"},
      {'I', "vlog 1"},
      {'I', "log info"},
      {'W', "log warning"},
      {'E', "log error"},
      {'I', "vlog_if -1"},
      {'I', "vlog_if 0"},
      {'I', "vlog_if 1"},
      {'I', "log_if info"},
      {'W', "log_if warning"},
      {'E', "log_if error"},
      {'I', "vlog_if 0 expr"},
      {'I', "log_if info expr"},
      {'E', "log_if error expr"},
      {'I', "log_if info every 1 expr"},
      {'E', "log_if error every 1 expr"},
      {'I', "vlog_if 0 every 1 expr"}}},
    {"StderrVerbosityMinus1ThresholdInfo",
     StandardStream::kStderr,
     -1,
     NGLOG_INFO,
     false,
     false,
     {{'I',
       "RAW: Test: v=-1 stderrthreshold=0 logtostderr=0 alsologtostderr=0"},
      {'I', "RAW: vlog -1"},
      {'I', "RAW: log info"},
      {'W', "RAW: log warning"},
      {'E', "RAW: log error"},
      {'I', "vlog -1"},
      {'I', "log info"},
      {'W', "log warning"},
      {'E', "log error"},
      {'I', "vlog_if -1"},
      {'I', "log_if info"},
      {'W', "log_if warning"},
      {'E', "log_if error"},
      {'I', "log_if info expr"},
      {'E', "log_if error expr"},
      {'I', "log_if info every 1 expr"},
      {'E', "log_if error every 1 expr"}}},
    {"StderrVerbosity0ThresholdWarning",
     StandardStream::kStderr,
     0,
     NGLOG_WARNING,
     false,
     false,
     {{'I', "RAW: Test: v=0 stderrthreshold=1 logtostderr=0 alsologtostderr=0"},
      {'W', "RAW: log warning"},
      {'E', "RAW: log error"},
      {'W', "log warning"},
      {'E', "log error"},
      {'W', "log_if warning"},
      {'E', "log_if error"},
      {'E', "log_if error expr"},
      {'E', "log_if error every 1 expr"}}},
    {"StderrVerbosity0ThresholdError",
     StandardStream::kStderr,
     0,
     NGLOG_ERROR,
     false,
     false,
     {{'I', "RAW: Test: v=0 stderrthreshold=2 logtostderr=0 alsologtostderr=0"},
      {'E', "RAW: log error"},
      {'E', "log error"},
      {'E', "log_if error"},
      {'E', "log_if error expr"},
      {'E', "log_if error every 1 expr"}}},
    {"StderrVerbosity0ThresholdFatal",
     StandardStream::kStderr,
     0,
     NGLOG_FATAL,
     false,
     false,
     {{'I',
       "RAW: Test: v=0 stderrthreshold=3 logtostderr=0 alsologtostderr=0"}}},
    {"StderrVerbosity0ThresholdFatalLogToStderr",
     StandardStream::kStderr,
     0,
     NGLOG_FATAL,
     true,
     false,
     {{'I', "RAW: Test: v=0 stderrthreshold=3 logtostderr=1 alsologtostderr=0"},
      {'I', "RAW: vlog -1"},
      {'I', "RAW: vlog 0"},
      {'I', "RAW: log info"},
      {'W', "RAW: log warning"},
      {'E', "RAW: log error"},
      {'I', "vlog -1"},
      {'I', "vlog 0"},
      {'I', "log info"},
      {'W', "log warning"},
      {'E', "log error"},
      {'I', "vlog_if -1"},
      {'I', "vlog_if 0"},
      {'I', "log_if info"},
      {'W', "log_if warning"},
      {'E', "log_if error"},
      {'I', "vlog_if 0 expr"},
      {'I', "log_if info expr"},
      {'E', "log_if error expr"},
      {'I', "log_if info every 1 expr"},
      {'E', "log_if error every 1 expr"},
      {'I', "vlog_if 0 every 1 expr"}}},
    {"StderrVerbosity0ThresholdFatalAlsoLogToStderr",
     StandardStream::kStderr,
     0,
     NGLOG_FATAL,
     false,
     true,
     {{'I', "RAW: Test: v=0 stderrthreshold=3 logtostderr=0 alsologtostderr=1"},
      {'I', "RAW: vlog -1"},
      {'I', "RAW: vlog 0"},
      {'I', "RAW: log info"},
      {'W', "RAW: log warning"},
      {'E', "RAW: log error"},
      {'I', "vlog -1"},
      {'I', "vlog 0"},
      {'I', "log info"},
      {'W', "log warning"},
      {'E', "log error"},
      {'I', "vlog_if -1"},
      {'I', "vlog_if 0"},
      {'I', "log_if info"},
      {'W', "log_if warning"},
      {'E', "log_if error"},
      {'I', "vlog_if 0 expr"},
      {'I', "log_if info expr"},
      {'E', "log_if error expr"},
      {'I', "log_if info every 1 expr"},
      {'E', "log_if error every 1 expr"},
      {'I', "vlog_if 0 every 1 expr"}}},
    {"StderrVerbosity1ThresholdWarning",
     StandardStream::kStderr,
     1,
     NGLOG_WARNING,
     false,
     false,
     {{'I', "RAW: Test: v=1 stderrthreshold=1 logtostderr=0 alsologtostderr=0"},
      {'W', "RAW: log warning"},
      {'E', "RAW: log error"},
      {'W', "log warning"},
      {'E', "log error"},
      {'W', "log_if warning"},
      {'E', "log_if error"},
      {'E', "log_if error expr"},
      {'E', "log_if error every 1 expr"}}},
    {"StderrVerbosity1ThresholdFatalAlsoLogToStderr",
     StandardStream::kStderr,
     1,
     NGLOG_FATAL,
     false,
     true,
     {{'I', "RAW: Test: v=1 stderrthreshold=3 logtostderr=0 alsologtostderr=1"},
      {'I', "RAW: vlog -1"},
      {'I', "RAW: vlog 0"},
      {'I', "RAW: vlog 1"},
      {'I', "RAW: log info"},
      {'W', "RAW: log warning"},
      {'E', "RAW: log error"},
      {'I', "vlog -1"},
      {'I', "vlog 0"},
      {'I', "vlog 1"},
      {'I', "log info"},
      {'W', "log warning"},
      {'E', "log error"},
      {'I', "vlog_if -1"},
      {'I', "vlog_if 0"},
      {'I', "vlog_if 1"},
      {'I', "log_if info"},
      {'W', "log_if warning"},
      {'E', "log_if error"},
      {'I', "vlog_if 0 expr"},
      {'I', "log_if info expr"},
      {'E', "log_if error expr"},
      {'I', "log_if info every 1 expr"},
      {'E', "log_if error every 1 expr"},
      {'I', "vlog_if 0 every 1 expr"}}},
    {"StdoutVerbosity0ThresholdInfo",
     StandardStream::kStdout,
     0,
     NGLOG_INFO,
     false,
     false,
     {}},
    {"StdoutVerbosity1ThresholdInfo",
     StandardStream::kStdout,
     1,
     NGLOG_INFO,
     false,
     false,
     {}},
    {"StdoutVerbosityMinus1ThresholdInfo",
     StandardStream::kStdout,
     -1,
     NGLOG_INFO,
     false,
     false,
     {}},
    {"StdoutVerbosity0ThresholdWarning",
     StandardStream::kStdout,
     0,
     NGLOG_WARNING,
     false,
     false,
     {{'I', "vlog -1"},
      {'I', "vlog 0"},
      {'I', "log info"},
      {'I', "vlog_if -1"},
      {'I', "vlog_if 0"},
      {'I', "log_if info"},
      {'I', "vlog_if 0 expr"},
      {'I', "log_if info expr"},
      {'I', "log_if info every 1 expr"},
      {'I', "vlog_if 0 every 1 expr"}}},
    {"StdoutVerbosity0ThresholdError",
     StandardStream::kStdout,
     0,
     NGLOG_ERROR,
     false,
     false,
     {{'I', "vlog -1"},
      {'I', "vlog 0"},
      {'I', "log info"},
      {'W', "log warning"},
      {'I', "vlog_if -1"},
      {'I', "vlog_if 0"},
      {'I', "log_if info"},
      {'W', "log_if warning"},
      {'I', "vlog_if 0 expr"},
      {'I', "log_if info expr"},
      {'I', "log_if info every 1 expr"},
      {'I', "vlog_if 0 every 1 expr"}}},
    {"StdoutVerbosity0ThresholdFatal",
     StandardStream::kStdout,
     0,
     NGLOG_FATAL,
     false,
     false,
     {{'I', "vlog -1"},
      {'I', "vlog 0"},
      {'I', "log info"},
      {'W', "log warning"},
      {'E', "log error"},
      {'I', "vlog_if -1"},
      {'I', "vlog_if 0"},
      {'I', "log_if info"},
      {'W', "log_if warning"},
      {'E', "log_if error"},
      {'I', "vlog_if 0 expr"},
      {'I', "log_if info expr"},
      {'E', "log_if error expr"},
      {'I', "log_if info every 1 expr"},
      {'E', "log_if error every 1 expr"},
      {'I', "vlog_if 0 every 1 expr"}}},
    {"StdoutVerbosity0ThresholdFatalLogToStderr",
     StandardStream::kStdout,
     0,
     NGLOG_FATAL,
     true,
     false,
     {{'I', "vlog -1"},
      {'I', "vlog 0"},
      {'I', "log info"},
      {'W', "log warning"},
      {'E', "log error"},
      {'I', "vlog_if -1"},
      {'I', "vlog_if 0"},
      {'I', "log_if info"},
      {'W', "log_if warning"},
      {'E', "log_if error"},
      {'I', "vlog_if 0 expr"},
      {'I', "log_if info expr"},
      {'E', "log_if error expr"},
      {'I', "log_if info every 1 expr"},
      {'E', "log_if error every 1 expr"},
      {'I', "vlog_if 0 every 1 expr"}}},
    {"StdoutVerbosity0ThresholdFatalAlsoLogToStderr",
     StandardStream::kStdout,
     0,
     NGLOG_FATAL,
     false,
     true,
     {{'I', "vlog -1"},
      {'I', "vlog 0"},
      {'I', "log info"},
      {'W', "log warning"},
      {'E', "log error"},
      {'I', "vlog_if -1"},
      {'I', "vlog_if 0"},
      {'I', "log_if info"},
      {'W', "log_if warning"},
      {'E', "log_if error"},
      {'I', "vlog_if 0 expr"},
      {'I', "log_if info expr"},
      {'E', "log_if error expr"},
      {'I', "log_if info every 1 expr"},
      {'E', "log_if error every 1 expr"},
      {'I', "vlog_if 0 every 1 expr"}}},
    {"StdoutVerbosity1ThresholdWarning",
     StandardStream::kStdout,
     1,
     NGLOG_WARNING,
     false,
     false,
     {{'I', "vlog -1"},
      {'I', "vlog 0"},
      {'I', "vlog 1"},
      {'I', "log info"},
      {'I', "vlog_if -1"},
      {'I', "vlog_if 0"},
      {'I', "vlog_if 1"},
      {'I', "log_if info"},
      {'I', "vlog_if 0 expr"},
      {'I', "log_if info expr"},
      {'I', "log_if info every 1 expr"},
      {'I', "vlog_if 0 every 1 expr"}}},
    {"StdoutVerbosity1ThresholdFatalAlsoLogToStderr",
     StandardStream::kStdout,
     1,
     NGLOG_FATAL,
     false,
     true,
     {{'I', "vlog -1"},
      {'I', "vlog 0"},
      {'I', "vlog 1"},
      {'I', "log info"},
      {'W', "log warning"},
      {'E', "log error"},
      {'I', "vlog_if -1"},
      {'I', "vlog_if 0"},
      {'I', "vlog_if 1"},
      {'I', "log_if info"},
      {'W', "log_if warning"},
      {'E', "log_if error"},
      {'I', "vlog_if 0 expr"},
      {'I', "log_if info expr"},
      {'E', "log_if error expr"},
      {'I', "log_if info every 1 expr"},
      {'E', "log_if error every 1 expr"},
      {'I', "vlog_if 0 every 1 expr"}}},

};

INSTANTIATE_TEST_SUITE_P(Flags, LoggingLevels, testing::ValuesIn(kLevelsCases),
                         CaseName{});

TEST_P(LoggingLevels, WritesMessagesThatTheFlagsEnable) {
  const LevelsCase& param = GetParam();
  nglog::ScopedFlags<> save;
  RouteLogMessagesTo(param.stream);

  ScopedStreamCapture capture{param.stream};
  LogWithLevels(param.verbosity, param.stderr_threshold, param.log_to_stderr,
                param.also_log_to_stderr);
  EXPECT_THAT(SplitLines(capture.Stop()),
              ElementsAreArray(LogLines(param.expected)));
}

TEST(VLOG, ModuleLevelOverridesVerbosityFlag) {
  constexpr int kVlogTestLevel = 1;
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_v, 0);
  EXPECT_FALSE(IsVerboseInTestModule(kVlogTestLevel));

#if defined(__GNUC__)
  const int previous_level = SetVLOGLevel(kVlogTestModule, kVlogTestLevel);
  EXPECT_EQ(previous_level, 0);
  EXPECT_TRUE(IsVerboseInTestModule(kVlogTestLevel));
  SetVLOGLevel(kVlogTestModule, previous_level);
#endif
}

#if defined(__GNUC__)
static void UpdateVLogLevelWhileReading() {
  constexpr int kInitialLevel = 0;
  constexpr int kUpdateCount = 100000;
  constexpr int kVlogTestLevel = 1;
  constexpr int kVlogLevelPeriod = 2;
  constexpr int kYieldPeriod = 64;
  SetVLOGLevel(kVlogTestModule, kInitialLevel);
  static_cast<void>(IsVerboseInTestModule(kVlogTestLevel));
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
      static_cast<void>(IsVerboseInTestModule(kVlogTestLevel));
    }
  });

  {
    std::unique_lock<std::mutex> lock(reader_started_mutex);
    reader_started_condition.wait(lock,
                                  [&reader_started] { return reader_started; });
  }

  for (int i = 0; i < kUpdateCount; ++i) {
    SetVLOGLevel(kVlogTestModule, i % kVlogLevelPeriod);
    if (i % kYieldPeriod == 0) {
      std::this_thread::yield();
    }
  }

  stop.store(true, std::memory_order_relaxed);
  reader.join();
  SetVLOGLevel(kVlogTestModule, kInitialLevel);
}

// Data races between the readers and the writer only fail the test under
// ThreadSanitizer.
TEST(VLOG, ConcurrentLevelUpdates) { UpdateVLogLevelWhileReading(); }

// Threads that reach a VLOG site together initialize it once. Threads that
// found the site uninitialized must not rewrite the cached level that others
// already read, which ThreadSanitizer reports as a data race.
TEST(VLOG, ConcurrentSiteInitialization) {
  constexpr int kThreads = 8;
  constexpr int kChecksPerThread = 100;
  constexpr int kVlogTestLevel = 1;
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_v, 0);
  std::mutex start_mutex;
  std::condition_variable start_condition;
  bool start = false;
  std::vector<int> verbose_checks(kThreads, 0);
  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int i = 0; i != kThreads; ++i) {
    threads.emplace_back(
        [&start_mutex, &start_condition, &start, &verbose_checks, i] {
          {
            std::unique_lock<std::mutex> lock(start_mutex);
            start_condition.wait(lock, [&start] { return start; });
          }
          for (int check = 0; check != kChecksPerThread; ++check) {
            if (VLOG_IS_ON(kVlogTestLevel)) {
              ++verbose_checks[static_cast<std::size_t>(i)];
            }
          }
        });
  }
  {
    std::lock_guard<std::mutex> lock(start_mutex);
    start = true;
  }
  start_condition.notify_all();
  for (std::thread& thread : threads) {
    thread.join();
  }
  EXPECT_THAT(verbose_checks, Each(0));
}
#endif
