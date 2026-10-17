// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Satoru Takabayashi
//
// This is a helper binary for testing signalhandler.cc.  Scenarios that
// terminate the process are invoked through command-line arguments.

#include <fcntl.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sys/types.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "ng-log/platform.h"
#include "stacktrace.h"
#include "symbolize.h"
#include "testing_utilities.h"
#include "utilities.h"

#if defined(HAVE_SUBPROCESS)
#  include "subprocess.h"
#endif
#if defined(HAVE_UNISTD_H)
#  include <unistd.h>
#endif
#if defined(NGLOG_OS_WINDOWS)
#  include <windows.h>
#else
#  include <dirent.h>
#  include <sys/stat.h>
#endif
#if defined(NGLOG_OS_LINUX)
#  include <pthread.h>
#  include <sys/syscall.h>
#endif  // defined(NGLOG_OS_LINUX)
#if !defined(NGLOG_OS_WINDOWS)
#  include <sys/wait.h>
#endif
#if defined(_MSC_VER)
#  include <io.h>  // write
#endif

using namespace nglog;

#if defined(NGLOG_OS_LINUX) && defined(HAVE_STACKTRACE) && \
    defined(HAVE_SYMBOLIZE) &&                             \
    (defined(HAVE_ADDR2LINE) || defined(HAVE_LIBBACKTRACE))
constexpr std::size_t kSignalBasePathLength = 173;
constexpr std::size_t kSymbolBufferSize = 1024;
constexpr int kAllocationInSignalHandler = 42;

// The failure signal handler must not allocate memory.
static void ReportSignalHandlerAllocation() {
  constexpr char kMessage[] = "allocation in signal handler\n";
  const ssize_t ignored = write(STDERR_FILENO, kMessage, sizeof(kMessage) - 1);
  static_cast<void>(ignored);
  _exit(kAllocationInSignalHandler);
}
#endif

#if defined(NGLOG_OS_LINUX)
static volatile sig_atomic_t g_suppress_default_sigterm = 0;

// Let a failure handler return after it restores SIGTERM's default disposition.
// This models environments where the default action is ignored, such as PID 1.
extern "C" int kill(pid_t pid, int signal_number) noexcept {
  if (g_suppress_default_sigterm != 0 && pid == getpid() &&
      signal_number == SIGTERM) {
    return 0;
  }
  return static_cast<int>(syscall(SYS_kill, pid, signal_number));
}
#endif  // defined(NGLOG_OS_LINUX)

// The path of this test binary, which runs the scenarios that need a process
// of their own.
static const char* g_self_path = nullptr;

// Bounds how long a child scenario may take before it counts as hung. A
// hung child never finishes, so the bound only needs to tolerate long
// scheduling stalls of a loaded machine, during which dumping and
// symbolizing a crash is slow. The self test runs four scenarios in a row,
// which together must stay below its CTest timeout of 60 seconds.
constexpr std::chrono::seconds kChildTimeout{12};

// Logged by the segv scenario right before it crashes.
constexpr char kMessageBeforeCrash[] = "a message before segv";

// Precedes the identifier of the dying thread in the output of
// DieInThread().
constexpr char kDyingThreadMarker[] = "dying thread: ";

// Printed by WriteAndReenter() right before it raises another signal.
constexpr char kReentryMarker[] = "reentering the failure signal handler";

// Precedes every chunk of the failure dump that WriteWithMarker() receives.
constexpr char kCustomWriterMarker[] = "custom writer: ";

static void DieInThread() {
  std::ostringstream oss;
  oss << std::showbase << std::hex << std::this_thread::get_id();

  fprintf(stderr, "%s%s\n", kDyingThreadMarker, oss.str().c_str());
  raise(SIGFPE);
}

static void WriteToStderr(const char* data, std::size_t size) {
  if (write(fileno(stderr), data, size) < 0) {
    // Ignore errors.
  }
}

// Marks every chunk of the failure dump to tell it apart from the output of
// the default writer.
static void WriteWithMarker(const char* data, std::size_t size) {
  WriteToStderr(kCustomWriterMarker, sizeof(kCustomWriterMarker) - 1);
  WriteToStderr(data, size);
}

// The first time this runs, it crashes again with a different signal
// while the first crash is still being dumped.
static void WriteAndReenter(const char* data, std::size_t size) {
  WriteToStderr(data, size);
  static std::atomic_flag entered = ATOMIC_FLAG_INIT;
  if (!entered.test_and_set()) {
    WriteToStderr(kReentryMarker, sizeof(kReentryMarker) - 1);
    WriteToStderr("\n", 1);
    raise(SIGTERM);
  }
}

static void CrashInThread() { raise(SIGSEGV); }

#if defined(NGLOG_OS_LINUX)
static void DiscardFailureDump(const char*, std::size_t) {}

static void ReturnFromFailureHandler() {
  g_suppress_default_sigterm = 1;
  raise(SIGTERM);
  g_suppress_default_sigterm = 0;
}
#endif  // defined(NGLOG_OS_LINUX)

#if !defined(NGLOG_OS_WINDOWS)
// Runs self_path with child_command as its only argument and waits for it
// to terminate within kChildTimeout. Returns false after killing the child
// if it does not. The child's own stdout is left connected to this
// process's, and so is its stderr unless stderr_fd is a descriptor to
// redirect it to.
static bool RunChild(const char* self_path, const char* child_command,
                     int* status, int stderr_fd = -1) {
  constexpr int kExecFailure = 127;
  constexpr useconds_t kPollIntervalUs = 20000;

  const pid_t pid = fork();
  if (pid == 0) {
    if (stderr_fd != -1 && dup2(stderr_fd, STDERR_FILENO) < 0) {
      _exit(kExecFailure);
    }
    execl(self_path, self_path, child_command, static_cast<char*>(nullptr));
    _exit(kExecFailure);
  }
  if (pid < 0) {
    fprintf(stderr, "%s: fork failed\n", child_command);
    return false;
  }

  const auto deadline = std::chrono::steady_clock::now() + kChildTimeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (waitpid(pid, status, WNOHANG) == pid) {
      return true;
    }
    usleep(kPollIntervalUs);
  }

  fprintf(stderr, "%s: timed out after %lld seconds\n", child_command,
          static_cast<long long>(kChildTimeout.count()));
  kill(pid, SIGKILL);
  waitpid(pid, status, 0);
  return false;
}

static void ReportUnexpectedStatus(const char* child_command, int status) {
  if (WIFSIGNALED(status)) {
    fprintf(stderr, "%s: died by unexpected signal %d\n", child_command,
            WTERMSIG(status));
  } else if (WIFEXITED(status)) {
    fprintf(stderr, "%s: exited with unexpected code %d\n", child_command,
            WEXITSTATUS(status));
  } else {
    fprintf(stderr, "%s: unexpected status 0x%x\n", child_command, status);
  }
}

// Checks that status reports the termination of the child scenario by one of
// acceptable_signals, and reports any other status. Used to verify the
// reentrancy guard in signalhandler.cc actually lets the process die instead of
// hanging. More than one acceptable signal matters whenever a second signal can
// legitimately fire while the first is still being dumped, whether deliberately
// (the reentrant scenario) or as a side effect of dumping itself (some
// platforms hit an internal abort() while symbolizing a std::thread-invoked
// frame). Once both handler invocations have returned, two pending unblocked
// signals race, and which one the kernel ends up delivering first is
// platform-dependent, not a correctness property of the guard.
static bool DiedBySignal(const char* child_command, int status,
                         std::initializer_list<int> acceptable_signals) {
  if (WIFSIGNALED(status)) {
    for (int signal : acceptable_signals) {
      if (WTERMSIG(status) == signal) {
        return true;
      }
    }
  }
  ReportUnexpectedStatus(child_command, status);
  return false;
}

// Runs the child scenario and checks how it terminated with DiedBySignal().
static bool ExpectChildDiesBySignal(
    const char* self_path, const char* child_command,
    std::initializer_list<int> acceptable_signals) {
  int status = 0;
  return RunChild(self_path, child_command, &status) &&
         DiedBySignal(child_command, status, acceptable_signals);
}

// Like ExpectChildDiesBySignal(), but additionally checks that the stderr
// output of the child contains expected_output. The output is passed on to
// this process's stderr afterwards so that it still shows up in the test log.
static bool ExpectChildDiesBySignalWithOutput(
    const char* self_path, const char* child_command,
    std::initializer_list<int> acceptable_signals,
    const char* expected_output) {
  const std::string path = TestTmpDir() + "/" + child_command + ".err";
  const FileDescriptor output{
      open(path.c_str(), O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR)};
  if (!output) {
    fprintf(stderr, "%s: cannot create %s: %s\n", child_command, path.c_str(),
            strerror(errno));
    return false;
  }

  int status = 0;
  const bool completed =
      RunChild(self_path, child_command, &status, output.get());

  std::ifstream input{path, std::ios::binary};
  const std::string contents{std::istreambuf_iterator<char>{input},
                             std::istreambuf_iterator<char>{}};
  WriteToStderr(contents.data(), contents.size());

  if (!completed) {
    return false;
  }
  if (contents.find(expected_output) == std::string::npos) {
    fprintf(stderr, "%s: output lacks \"%s\"\n", child_command,
            expected_output);
    return false;
  }
  return DiedBySignal(child_command, status, acceptable_signals);
}

#  if defined(NGLOG_OS_LINUX)
static bool ExpectChildExitsSuccessfully(const char* self_path,
                                         const char* child_command) {
  int status = 0;
  if (!RunChild(self_path, child_command, &status)) {
    return false;
  }
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    return true;
  }
  ReportUnexpectedStatus(child_command, status);
  return false;
}
#  endif  // defined(NGLOG_OS_LINUX)

#  if defined(NGLOG_OS_LINUX)
struct ColorlogtostderrResult {
  bool completed = false;
  int status = 0;
  std::string output;
  std::string error;
};

static ColorlogtostderrResult RunColorlogtostderrChild(const char* self_path) {
  constexpr int kChildExecFailure = 127;
  constexpr std::size_t kOutputBufferSize = 256;
  ColorlogtostderrResult result;
  FileDescriptor master{posix_openpt(O_RDWR | O_NOCTTY)};
  if (!master || grantpt(master.get()) != 0 || unlockpt(master.get()) != 0) {
    result.error = "could not create a pseudo-terminal: ";
    result.error += strerror(errno);
    return result;
  }

  const char* const slave_name = ptsname(master.get());
  FileDescriptor slave{
      slave_name == nullptr ? -1 : open(slave_name, O_RDWR | O_NOCTTY)};
  if (!slave) {
    result.error = "could not open the pseudo-terminal slave: ";
    result.error += strerror(errno);
    return result;
  }

  const pid_t pid = fork();
  if (pid == 0) {
    if (dup2(slave.get(), fileno(stderr)) < 0) {
      _exit(kChildExecFailure);
    }
    close(master.get());
    close(slave.get());
    unsetenv("NO_COLOR");
    setenv("TERM", "xterm", 1);
    execl(self_path, self_path, "colorlogtostderr_off",
          static_cast<char*>(nullptr));
    _exit(kChildExecFailure);
  }
  slave.reset();

  if (pid < 0) {
    result.error = "fork failed: ";
    result.error += strerror(errno);
    return result;
  }

  // Drains the output while the child runs, so that it never blocks on a
  // full pseudo-terminal. Reading fails with EIO once the child and every
  // process it spawned have closed the slave.
  char buffer[kOutputBufferSize];
  for (;;) {
    const ssize_t bytes_read = read(master.get(), buffer, sizeof(buffer));
    if (bytes_read > 0) {
      result.output.append(buffer, static_cast<std::size_t>(bytes_read));
      continue;
    }
    if (bytes_read < 0 && errno == EINTR) {
      continue;
    }
    if (bytes_read < 0 && errno != EIO) {
      result.error = "read failed: ";
      result.error += strerror(errno);
    }
    break;
  }

  if (waitpid(pid, &result.status, 0) != pid) {
    result.error = "waitpid failed: ";
    result.error += strerror(errno);
  }
  result.completed = result.error.empty();
  return result;
}
#  endif  // defined(NGLOG_OS_LINUX)
#endif    // !defined(NGLOG_OS_WINDOWS)

#if defined(HAVE_SUBPROCESS)
// The outcome of a run of this test binary.
struct ChildRun {
  SubprocessWaitResult wait = SubprocessWaitResult::Failed();
  std::string output;
};

// Runs this test binary with arguments, collects its stdout and waits for it
// to terminate within kChildTimeout. Its stderr is discarded.
static ChildRun RunSelf(std::initializer_list<std::string> arguments) {
  constexpr std::size_t kReadBufferSize = 256;
  std::vector<std::string> storage{g_self_path};
  storage.insert(storage.end(), arguments);
  std::vector<char*> argv;
  for (std::string& argument : storage) {
    argv.push_back(&argument[0]);
  }
  argv.push_back(nullptr);
  char* envp[] = {nullptr};

  ChildRun run;
  Subprocess<> process;
  if (!process.Spawn(argv.data(), envp)) {
    return run;
  }
  process.CloseStdin();

  const auto deadline = std::chrono::steady_clock::now() + kChildTimeout;
  const auto remaining = [&deadline] {
    return std::max(std::chrono::milliseconds::zero(),
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        deadline - std::chrono::steady_clock::now()));
  };
  char buffer[kReadBufferSize];
  for (;;) {
    const std::size_t bytes_read =
        process.ReadStdout(buffer, sizeof(buffer), remaining());
    if (bytes_read == 0) {
      break;
    }
    run.output.append(buffer, bytes_read);
  }
  run.wait = process.Wait(remaining());
  return run;
}

// Returns the concatenated contents of the regular files in directory,
// skipping symbolic links to them.
static std::string ReadFilesIn(const std::string& directory) {
  std::vector<std::string> paths;
#  if defined(NGLOG_OS_WINDOWS)
  WIN32_FIND_DATAA entry;
  const HANDLE find = FindFirstFileA((directory + "\\*").c_str(), &entry);
  if (find != INVALID_HANDLE_VALUE) {
    do {
      if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        paths.push_back(directory + "\\" + entry.cFileName);
      }
    } while (FindNextFileA(find, &entry));
    FindClose(find);
  }
#  else
  DIR* const dir = opendir(directory.c_str());
  if (dir != nullptr) {
    while (const dirent* const entry = readdir(dir)) {
      const std::string path = directory + "/" + entry->d_name;
      struct stat file_status;
      if (lstat(path.c_str(), &file_status) == 0 &&
          S_ISREG(file_status.st_mode)) {
        paths.push_back(path);
      }
    }
    closedir(dir);
  }
#  endif  // defined(NGLOG_OS_WINDOWS)

  std::string contents;
  for (const std::string& path : paths) {
    std::ifstream input{path, std::ios::binary};
    contents.append(std::istreambuf_iterator<char>{input},
                    std::istreambuf_iterator<char>{});
  }
  return contents;
}

TEST(SignalHandler, NotInstalledBeforeInstallation) {
  const ChildRun run = RunSelf({"installed"});
  ASSERT_EQ(run.wait.status, SubprocessWaitResult::kExited);
  EXPECT_EQ(run.wait.exit_code, 0);
  EXPECT_THAT(run.output,
              testing::HasSubstr("signal handler installed: false"));
}

// The handler flushes buffered log messages before the process dies.
TEST(SignalHandler, FlushesLogFilesBeforeDying) {
  const std::string log_dir = TestTmpDir();
  const ChildRun run = RunSelf({"--log_dir=" + log_dir, "segv"});
  ASSERT_NE(run.wait.status, SubprocessWaitResult::kTimedOut);
  EXPECT_FALSE(run.wait.status == SubprocessWaitResult::kExited &&
               run.wait.exit_code == 0);
  EXPECT_THAT(ReadFilesIn(log_dir), testing::HasSubstr(kMessageBeforeCrash));
}
#endif  // defined(HAVE_SUBPROCESS)

#if defined(HAVE_STACKTRACE) && defined(HAVE_SYMBOLIZE)
TEST(SignalHandler, InstalledFailureWriterReceivesDump) {
  ASSERT_DEATH(
      {
        InstallFailureWriter(&WriteWithMarker);
        abort();
      },
      "custom writer: \\*\\*\\* Aborted at ");
}

#  if defined(HAVE_SIGACTION) && !defined(NGLOG_OS_WINDOWS)
namespace {

void ReplacementSignalHandler(int) {}

}  // namespace

TEST(SignalHandler, InstalledStateIsIndependentOfSignalDisposition) {
  struct sigaction original_action;
  ASSERT_EQ(sigaction(SIGABRT, nullptr, &original_action), 0);

  struct sigaction replacement_action;
  memset(&replacement_action, 0, sizeof(replacement_action));
  sigemptyset(&replacement_action.sa_mask);
  replacement_action.sa_handler = &ReplacementSignalHandler;
  ASSERT_EQ(sigaction(SIGABRT, &replacement_action, nullptr), 0);

  EXPECT_TRUE(IsFailureSignalHandlerInstalled());
  ASSERT_EQ(sigaction(SIGABRT, &original_action, nullptr), 0);
}
#  endif  // defined(HAVE_SIGACTION) && !defined(NGLOG_OS_WINDOWS)

#  if defined(HAVE_SIGACTION) && !defined(NGLOG_OS_WINDOWS)
namespace {

// Matches failure output in which the value printed after marker up to the
// end of its line follows field as well, delimited by a space, a closing
// parenthesis or a semicolon.
MATCHER_P2(RepeatsPrintedValue, marker, field,
           std::string{negation ? "does not repeat" : "repeats"} +
               " the value printed after \"" + marker + "\" after \"" + field +
               "\"") {
  const std::string& output = arg;
  const std::size_t marker_position = output.find(marker);
  if (marker_position == std::string::npos) {
    *result_listener << "lacks \"" << marker << "\"";
    return false;
  }
  const std::size_t value_position = marker_position + std::strlen(marker);
  const std::size_t value_end = output.find('\n', value_position);
  const std::string expected =
      field + output.substr(value_position, value_end - value_position);
  constexpr char kDelimiters[] = " );";
  for (std::size_t position = output.find(expected);
       position != std::string::npos;
       position = output.find(expected, position + 1)) {
    const std::size_t end = position + expected.size();
    if (end < output.size() &&
        std::memchr(kDelimiters, output[end], sizeof(kDelimiters) - 1) !=
            nullptr) {
      return true;
    }
  }
  *result_listener << "lacks \"" << expected << "\"";
  return false;
}

}  // namespace

// The dump identifies the thread that received the signal.
TEST(SignalHandler, DumpReportsDyingThread) {
  ASSERT_DEATH(
      {
        std::thread thread{&DieInThread};
        thread.join();
      },
      testing::Matcher<const std::string&>(
          RepeatsPrintedValue(kDyingThreadMarker, "(TID ")));
}

TEST(SignalHandler, FailureDumpOrder) {
  ASSERT_DEATH(CrashInThread(),
               "\\*\\*\\* Aborted at [^\n]*\n"
               "\\*\\*\\* SIGSEGV [^\n]*\n"
               "PC: ");
}
#  endif  // defined(HAVE_SIGACTION) && !defined(NGLOG_OS_WINDOWS)

#  if defined(HAVE_SIGACTION) && defined(NGLOG_OS_LINUX) && defined(__x86_64__)
// Faults on its first instruction, so that the address before the faulting
// instruction belongs to another function.
extern "C" void nglog_test_trap_on_entry();
asm(".text\n"
    ".globl nglog_test_trap_on_entry\n"
    ".type nglog_test_trap_on_entry, @function\n"
    "nglog_test_trap_on_entry:\n"
    "  ud2\n"
    ".size nglog_test_trap_on_entry, .-nglog_test_trap_on_entry\n");

// Unlike the return addresses of the frames below it, the program counter
// of the fault points at the faulting instruction itself.
TEST(SignalHandler, FaultingInstructionNamesItsFunction) {
  ASSERT_DEATH(nglog_test_trap_on_entry(),
               "PC: @ +0x[0-9a-f]+ +([^ ]+:[0-9]+ )?nglog_test_trap_on_entry");
}

// The stack trace lists the interrupted frame by the faulting address as
// well.
TEST(SignalHandler, FaultingFrameNamesItsFunction) {
  ASSERT_DEATH(nglog_test_trap_on_entry(),
               "\n +@ +0x[0-9a-f]+ +([^ ]+:[0-9]+ )?nglog_test_trap_on_entry");
}

// A fault has an address but no sending process, whose field shares storage
// with the address.
TEST(SignalHandler, FaultReportsAddressWithoutSender) {
  ASSERT_DEATH(nglog_test_trap_on_entry(),
               "\\*\\*\\* SIGILL \\(@0x[0-9a-f]+\\) received by PID [0-9]+ "
               "\\(TID [^)]*\\) stack trace: \\*\\*\\*");
}

// A signal sent by a process has a sender but no fault address.
TEST(SignalHandler, SentSignalReportsSenderWithoutAddress) {
  ASSERT_DEATH(CrashInThread(),
               "\\*\\*\\* SIGSEGV received by PID [0-9]+ \\(TID [^)]*\\) "
               "from PID [0-9]+; stack trace: \\*\\*\\*");
}
#  endif

#  if defined(HAVE_SIGACTION) && defined(NGLOG_OS_LINUX)
constexpr char kReceivingProcessMarker[] = "receiving PID: ";
constexpr char kSendingProcessMarker[] = "sending PID: ";

// Lets a child process send SIGTERM to this process, which only handles the
// signal after it printed both process IDs.
static void TerminateFromAnotherProcess() {
  sigset_t terminate;
  sigemptyset(&terminate);
  sigaddset(&terminate, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &terminate, nullptr);

  const pid_t receiver = getpid();
  const pid_t sender = fork();
  if (sender == 0) {
    kill(receiver, SIGTERM);
    _exit(EXIT_SUCCESS);
  }
  fprintf(stderr, "%s%ld\n%s%ld\n", kReceivingProcessMarker,
          static_cast<long>(receiver), kSendingProcessMarker,
          static_cast<long>(sender));
  waitpid(sender, nullptr, 0);
  pthread_sigmask(SIG_UNBLOCK, &terminate, nullptr);
}

TEST(SignalHandler, SentSignalReportsSendingProcess) {
  EXPECT_EXIT(
      TerminateFromAnotherProcess(), testing::KilledBySignal(SIGTERM),
      testing::Matcher<const std::string&>(testing::AllOf(
          testing::HasSubstr("*** SIGTERM received by PID "),
          RepeatsPrintedValue(kReceivingProcessMarker, "received by PID "),
          RepeatsPrintedValue(kSendingProcessMarker, "from PID "))));
}
#  endif  // defined(HAVE_SIGACTION) && defined(NGLOG_OS_LINUX)

#  if defined(NGLOG_OS_WINDOWS) && !defined(HAVE_ADDR2LINE)
TEST(SignalHandler, SourceLocationPrecedesFunctionName) {
  ASSERT_DEATH(abort(), "signalhandler_unittest\\.cc:.*TestBody");
}
#  endif  // defined(NGLOG_OS_WINDOWS) && !defined(HAVE_ADDR2LINE)
#endif    // defined(HAVE_STACKTRACE) && defined(HAVE_SYMBOLIZE)

#if defined(HAVE_STACKTRACE)
// Recurses `depth` frames before raising SIGSEGV.
NGLOG_ATTRIBUTE_NOINLINE static void CrashAtDepth(int depth) {
  if (depth > 0) {
    CrashAtDepth(depth - 1);
    NGLOG_BLOCK_TAIL_CALL_OPTIMIZATION();
    return;
  }
  raise(SIGSEGV);
}

TEST(SignalHandler, SaysWhenMoreFramesFollow) {
  if (!UnwinderWalksPastMaxStackDumpFrames()) {
    GTEST_SKIP() << "the unwinder stops before kMaxStackDumpFrames";
  }
  ASSERT_DEATH(CrashAtDepth(kDepthPastStackDumpLimit),
               HasCutOffStackDump(kMaxStackDumpFrames));
}
#endif  // defined(HAVE_STACKTRACE)

#if defined(NGLOG_OS_LINUX)
TEST(SignalHandler, ColorlogtostderrDisablesAnsi) {
  ASSERT_NE(g_self_path, nullptr);
  const ColorlogtostderrResult result = RunColorlogtostderrChild(g_self_path);
  ASSERT_TRUE(result.completed) << result.error;
  EXPECT_PRED1(testing::KilledBySignal(SIGABRT), result.status);
  EXPECT_THAT(result.output, testing::Not(testing::HasSubstr("\033[")));
}

#  if defined(HAVE_STACKTRACE) && defined(HAVE_SYMBOLIZE) && \
      (defined(HAVE_ADDR2LINE) || defined(HAVE_LIBBACKTRACE))
TEST(SignalHandler, HyperlinkedStackFramesDoNotAllocate) {
#    ifdef NGLOG_TEST_NO_ALLOCATION_HOOKS
  GTEST_SKIP() << "the allocation hooks are unavailable";
#    endif
  // Hyperlinks need source locations, which builds without debug information
  // lack.
  char symbol[kSymbolBufferSize];
  SymbolizedFrame frame;
  if (!Symbolize(reinterpret_cast<void*>(&CrashInThread), symbol,
                 sizeof(symbol), SymbolizeOptions::kNone, &frame) ||
      frame.file_line_length == 0) {
    GTEST_SKIP() << "source locations are unavailable";
  }
  EXPECT_EXIT(
      {
        unsetenv("NO_COLOR");
        setenv("CLICOLOR_FORCE", "1", 1);
        setenv("VTE_VERSION", "1", 1);
        nglog::SetFlag(FLAGS_colorlogtostderr, true);
        nglog::SetFlag(FLAGS_symbolize_hyperlinks, true);
        nglog::SetFlag(FLAGS_symbolize_file_base_path,
                       std::string(kSignalBasePathLength, 'b'));
        InstallFailureSignalHandler();
        nglog::g_new_hook = &ReportSignalHandlerAllocation;
        raise(SIGSEGV);
      },
      // The dump must contain hyperlinks, or the test checks nothing.
      testing::KilledBySignal(SIGSEGV), "\\]8;;file://");
}
#  endif

TEST(SignalHandler, ThreadNameInFailureDump) {
  constexpr char kThreadName[] = "named-crash";
  ASSERT_EQ(pthread_setname_np(pthread_self(), kThreadName), 0);
  ASSERT_DEATH(abort(), "thread \\\"named-crash\\\"");
}
#endif  // defined(NGLOG_OS_LINUX)

int main(int argc, char** argv) {
#if defined(HAVE_STACKTRACE) && defined(HAVE_SYMBOLIZE)
  g_self_path = argv[0];
  // The tests match plain dumps, whatever color settings the environment
  // carries. Tests of colored dumps override this themselves.
  const nglog::ScopedEnvironmentVariable no_color{"NO_COLOR", "1"};
  InitializeLogging(argv[0]);
  // A shared GoogleMock library carries its own copy of GoogleTest, which
  // InitGoogleMock initializes instead of the one the tests run in.
  testing::InitGoogleTest(&argc, argv);
  testing::InitGoogleMock(&argc, argv);
  nglog::ParseCommandLineFlags(&argc, &argv,
                               nglog::FlagParsingMode::kRemoveFlags);
  const std::string command = argc > 1 ? argv[1] : "none";
  if (command == "colorlogtostderr_off") {
    nglog::SetFlag(FLAGS_colorlogtostderr, false);
    InstallFailureSignalHandler();
    abort();
  } else if (command == "installed") {
    printf("signal handler installed: %s\n",
           IsFailureSignalHandlerInstalled() ? "true" : "false");
    return 0;
  }

  InstallFailureSignalHandler();
  if (command == "segv") {
    // The first message is written as soon as the log file is created, while
    // the second one stays buffered until the failure handler flushes it.
    LOG(INFO) << "create the log file";
    LOG(INFO) << kMessageBeforeCrash;
    // Raise the signal directly so this test does not rely on undefined
    // behavior to exercise the failure handler.
    raise(SIGSEGV);
  } else if (command == "reentrant") {
    InstallFailureWriter(&WriteAndReenter);
    abort();
  } else if (command == "multithread") {
    constexpr std::size_t kCrashingThreads = 4;
    std::array<std::thread, kCrashingThreads> threads;
    for (std::thread& thread : threads) {
      thread = std::thread{&CrashInThread};
    }
    for (std::thread& thread : threads) {
      thread.join();
    }
#  if defined(NGLOG_OS_LINUX)
  } else if (command == "handler_returns") {
    InstallFailureWriter(&DiscardFailureDump);
    std::thread first{&ReturnFromFailureHandler};
    first.join();

    // The first handler restored the default disposition. Install it again and
    // verify that ownership was released before the first thread exited.
    InstallFailureSignalHandler();
    ReturnFromFailureHandler();
#  endif  // defined(NGLOG_OS_LINUX)
#  if !defined(NGLOG_OS_WINDOWS)
  } else if (command == "self_test") {
    const bool ok =
        ExpectChildDiesBySignal(argv[0], "segv", {SIGSEGV}) &&
        ExpectChildDiesBySignalWithOutput(argv[0], "reentrant",
                                          {SIGTERM, SIGABRT}, kReentryMarker) &&
        ExpectChildDiesBySignal(argv[0], "multithread", {SIGSEGV, SIGABRT})
#    if defined(NGLOG_OS_LINUX)
        && ExpectChildExitsSuccessfully(argv[0], "handler_returns")
#    endif  // defined(NGLOG_OS_LINUX)
        ;
    puts(ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
#  endif  // !defined(NGLOG_OS_WINDOWS)
  } else {
    return RUN_ALL_TESTS();
  }
#endif
  return 0;
}
