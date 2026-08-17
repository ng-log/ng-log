// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "subprocess.h"

// addr2line may run from a crash signal handler. Before exec, a child of a
// multithreaded process must use only async-signal-safe functions because other
// threads may have held locks. See signal-safety(7) and pthread_atfork(3).
//
// The signal-safe specialization uses _Fork() and execv(). _Fork(3) excludes
// pthread_atfork handlers, and signal-safety(7) lists both functions as safe.
// It uses a cached absolute path. Normal subprocesses use posix_spawn(), or the
// shared fork() and execvp() setup without the signal-safe guarantee.

#if defined(HAVE_SUBPROCESS) && !defined(NGLOG_OS_WINDOWS) && \
    (defined(HAVE_FORK) || (defined(HAVE__FORK) && defined(HAVE_EXECV)))

#  include <fcntl.h>
#  include <poll.h>
#  include <signal.h>  // NOLINT(modernize-deprecated-headers): POSIX SIGKILL
#  include <sys/wait.h>
#  include <unistd.h>

#  include <cerrno>
#  include <limits>
#  include <utility>

#  include "subprocess_fork.h"

namespace nglog {

namespace {

constexpr int kChildExecFailureStatus = 127;
#  if defined(HAVE__FORK) && defined(HAVE_EXECV)
constexpr int kWaitPollIntervalMilliseconds = 10;
#  endif

#  if defined(HAVE__FORK) && defined(HAVE_EXECV)
int RetryPoll(struct pollfd* pfd, int timeout) noexcept {
  int result;
  do {
    result = ::poll(pfd, 1, timeout);
  } while (result == -1 && errno == EINTR);
  return result;
}

int RetryPoll(int timeout) noexcept {
  int result;
  do {
    result = ::poll(nullptr, 0, timeout);
  } while (result == -1 && errno == EINTR);
  return result;
}

ssize_t RetryRead(int fd, void* buffer, std::size_t size) noexcept {
  ssize_t result;
  do {
    result = ::read(fd, buffer, size);
  } while (result == -1 && errno == EINTR);
  return result;
}
#  endif

ssize_t RetryWrite(int fd, const void* data, std::size_t size) noexcept {
  ssize_t result;
  do {
    result = ::write(fd, data, size);
  } while (result == -1 && errno == EINTR);
  return result;
}

#  if defined(HAVE__FORK) && defined(HAVE_EXECV)
int RetryKill(pid_t pid, int signal) noexcept {
  int result;
  do {
    result = ::kill(pid, signal);
  } while (result == -1 && errno == EINTR);
  return result;
}

pid_t RetryWaitPid(pid_t pid, int* status, int options) noexcept {
  pid_t result;
  do {
    result = ::waitpid(pid, status, options);
  } while (result == -1 && errno == EINTR);
  return result;
}

bool SetCloseOnExec(int fd) noexcept {
  const int flags = ::fcntl(fd, F_GETFD);
  return flags != -1 && ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != -1;
}

int ClampPollTimeout(std::chrono::milliseconds timeout) noexcept {
  using Milliseconds = std::chrono::milliseconds;
  constexpr auto kMaximum =
      static_cast<Milliseconds::rep>(std::numeric_limits<int>::max());
  const Milliseconds::rep count = timeout.count();
  if (count <= 0) {
    return 0;
  }
  if (count >= kMaximum) {
    return static_cast<int>(kMaximum);
  }
  return static_cast<int>(count);
}

bool CreateCloseOnExecPipe(int fds[2]) noexcept {
  // Keep this as pipe() plus fcntl(). signal-safety(7) lists both as safe,
  // but not pipe2(). This function is called by the signal-safe Spawn().
  if (::pipe(fds) != 0) {
    return false;
  }

  if (!SetCloseOnExec(fds[0]) || !SetCloseOnExec(fds[1])) {
    ::close(fds[0]);
    ::close(fds[1]);
    return false;
  }

  return true;
}

#  endif

void WriteExecError(int fd, int error) noexcept {
  RetryWrite(fd, &error, sizeof(error));
}

bool DuplicateForChild(int source, int target) noexcept {
  if (::dup2(source, target) < 0) {
    return false;
  }

  if (source != target) {
    return true;
  }

  const int flags = ::fcntl(target, F_GETFD);
  return flags != -1 && ::fcntl(target, F_SETFD, flags & ~FD_CLOEXEC) != -1;
}

}  // namespace

namespace internal {

#  if defined(HAVE_EXECV)
int ExecuteWithExecv(char* const argv[], char* const[] /*envp*/) {
  return ::execv(argv[0], argv);
}
#  endif

#  if defined(HAVE_FORK)
int ExecuteWithExecvp(char* const argv[], char* const[] /*envp*/) {
  return ::execvp(argv[0], argv);
}
#  endif

pid_t SpawnProcessWithFork(char* const argv[], char* const envp[], int stdin_fd,
                           int stdout_fd, int exec_error_fd,
                           ForkFunction fork_process,
                           ExecFunction exec_process) noexcept {
  const pid_t pid = fork_process();

  if (pid < 0) {
    return -1;
  }

  if (pid == 0) {
    if (!DuplicateForChild(stdin_fd, STDIN_FILENO) ||
        !DuplicateForChild(stdout_fd, STDOUT_FILENO)) {
      const int error = errno;
      WriteExecError(exec_error_fd, error);
      _exit(kChildExecFailureStatus);
    }

    const int dev_null = open("/dev/null", O_WRONLY);
    if (dev_null >= 0) {
      dup2(dev_null, STDERR_FILENO);
    }

    exec_process(argv, envp);
    const int error = errno;
    WriteExecError(exec_error_fd, error);
    _exit(kChildExecFailureStatus);
  }

  return pid;
}

}  // namespace internal

inline namespace tools {

#  if defined(HAVE__FORK) && defined(HAVE_EXECV)

template <>
Subprocess<SubprocessMode::kSignalSafe>::Subprocess(
    Subprocess&& other) noexcept;
template <>
Subprocess<SubprocessMode::kSignalSafe>&
Subprocess<SubprocessMode::kSignalSafe>::operator=(Subprocess&& other) noexcept;
template <>
void Subprocess<SubprocessMode::kSignalSafe>::Reset() noexcept;
template <>
bool Subprocess<SubprocessMode::kSignalSafe>::Spawn(
    char* const argv[], char* const envp[]) noexcept;
template <>
Subprocess<SubprocessMode::kSignalSafe>::operator bool() const noexcept;
template <>
std::size_t Subprocess<SubprocessMode::kSignalSafe>::WriteStdin(
    const char* data, std::size_t size,
    std::chrono::milliseconds timeout) noexcept;
template <>
void Subprocess<SubprocessMode::kSignalSafe>::CloseStdin() noexcept;
template <>
std::size_t Subprocess<SubprocessMode::kSignalSafe>::ReadStdout(
    char* out, std::size_t out_size,
    std::chrono::milliseconds timeout) noexcept;
template <>
SubprocessWaitResult Subprocess<SubprocessMode::kSignalSafe>::Wait(
    std::chrono::milliseconds timeout) noexcept;

template <>
Subprocess<SubprocessMode::kSignalSafe>::Subprocess(Subprocess&& other) noexcept
    : pid_{other.pid_},
      stdin_write_{std::move(other.stdin_write_)},
      stdout_read_{std::move(other.stdout_read_)} {
  other.pid_ = -1;
}

template <>
Subprocess<SubprocessMode::kSignalSafe>&
Subprocess<SubprocessMode::kSignalSafe>::operator=(
    Subprocess&& other) noexcept {
  if (this != &other) {
    Reset();
    pid_ = other.pid_;
    stdin_write_ = std::move(other.stdin_write_);
    stdout_read_ = std::move(other.stdout_read_);
    other.pid_ = -1;
  }

  return *this;
}

template <>
void Subprocess<SubprocessMode::kSignalSafe>::Reset() noexcept {
  const pid_t pid = pid_;
  pid_ = -1;

  if (pid >= 0) {
    RetryKill(pid, SIGKILL);
    int status = 0;
    RetryWaitPid(pid, &status, 0);
  }

  CloseStdin();

  stdout_read_.reset();
}

template <>
bool Subprocess<SubprocessMode::kSignalSafe>::Spawn(
    char* const argv[], char* const envp[]) noexcept {
  Reset();

  int stdin_fds[2];
  if (!CreateCloseOnExecPipe(stdin_fds)) {
    return false;
  }

  int stdout_fds[2];
  if (!CreateCloseOnExecPipe(stdout_fds)) {
    ::close(stdin_fds[0]);
    ::close(stdin_fds[1]);
    return false;
  }

  int exec_error_fds[2];
  if (!CreateCloseOnExecPipe(exec_error_fds)) {
    ::close(stdin_fds[0]);
    ::close(stdin_fds[1]);
    ::close(stdout_fds[0]);
    ::close(stdout_fds[1]);
    return false;
  }

  const pid_t pid = internal::SpawnProcessWithFork(
      argv, envp, stdin_fds[0], stdout_fds[1], exec_error_fds[1], &_Fork,
      &internal::ExecuteWithExecv);

  ::close(stdin_fds[0]);
  ::close(stdout_fds[1]);
  ::close(exec_error_fds[1]);

  if (pid < 0) {
    ::close(stdin_fds[1]);
    ::close(stdout_fds[0]);
    ::close(exec_error_fds[0]);
    return false;
  }

  int exec_error = 0;
  const ssize_t exec_error_bytes =
      RetryRead(exec_error_fds[0], &exec_error, sizeof(exec_error));
  ::close(exec_error_fds[0]);

  if (exec_error_bytes != 0) {
    RetryKill(pid, SIGKILL);
    int status = 0;
    RetryWaitPid(pid, &status, 0);
    ::close(stdin_fds[1]);
    ::close(stdout_fds[0]);
    return false;
  }

  pid_ = pid;
  stdin_write_.reset(stdin_fds[1]);
  stdout_read_.reset(stdout_fds[0]);
  return true;
}

template <>
Subprocess<SubprocessMode::kSignalSafe>::operator bool() const noexcept {
  return pid_ >= 0;
}

template <>
std::size_t Subprocess<SubprocessMode::kSignalSafe>::WriteStdin(
    const char* data, std::size_t size,
    std::chrono::milliseconds timeout) noexcept {
  if (!stdin_write_) {
    return 0;
  }

  struct pollfd pfd{};
  pfd.fd = stdin_write_.get();
  pfd.events = POLLOUT;
  if (RetryPoll(&pfd, ClampPollTimeout(timeout)) <= 0) {
    return 0;
  }

  const ssize_t written = RetryWrite(stdin_write_.get(), data, size);
  return written > 0 ? static_cast<std::size_t>(written) : 0;
}

template <>
void Subprocess<SubprocessMode::kSignalSafe>::CloseStdin() noexcept {
  stdin_write_.reset();
}

template <>
std::size_t Subprocess<SubprocessMode::kSignalSafe>::ReadStdout(
    char* out, std::size_t out_size,
    std::chrono::milliseconds timeout) noexcept {
  if (!stdout_read_ || out_size == 0) {
    return 0;
  }

  struct pollfd pfd{};
  pfd.fd = stdout_read_.get();
  pfd.events = POLLIN;
  if (RetryPoll(&pfd, ClampPollTimeout(timeout)) <= 0) {
    return 0;
  }

  const ssize_t bytes_read = RetryRead(stdout_read_.get(), out, out_size);
  return bytes_read > 0 ? static_cast<std::size_t>(bytes_read) : 0;
}

template <>
SubprocessWaitResult Subprocess<SubprocessMode::kSignalSafe>::Wait(
    std::chrono::milliseconds timeout) noexcept {
  if (pid_ < 0) {
    return SubprocessWaitResult::Failed();
  }

  auto remaining_milliseconds = timeout.count();
  int status = 0;
  bool exited = false;
  bool wait_failed = false;

  for (;;) {
    const pid_t result = RetryWaitPid(pid_, &status, WNOHANG);
    if (result == pid_) {
      exited = true;
      break;
    }

    if (result < 0) {
      wait_failed = true;
      break;
    }

    if (remaining_milliseconds <= 0) {
      break;
    }

    const int wait_milliseconds =
        remaining_milliseconds >= kWaitPollIntervalMilliseconds
            ? kWaitPollIntervalMilliseconds
            : static_cast<int>(remaining_milliseconds);
    if (RetryPoll(wait_milliseconds) < 0) {
      wait_failed = true;
      break;
    }
    remaining_milliseconds -= wait_milliseconds;
  }

  if (!exited) {
    const pid_t pid = pid_;
    const int kill_result = RetryKill(pid, SIGKILL);
    const int kill_error = errno;
    const pid_t wait_result = RetryWaitPid(pid, &status, 0);
    pid_ = -1;

    if (wait_result != pid) {
      return SubprocessWaitResult::Failed();
    }
    if (wait_failed || (kill_result < 0 && kill_error != ESRCH)) {
      return SubprocessWaitResult::Failed();
    }
    if (kill_result < 0) {
      return WIFEXITED(status)
                 ? SubprocessWaitResult::Exited(WEXITSTATUS(status))
                 : SubprocessWaitResult::Failed();
    }
    return SubprocessWaitResult::TimedOut();
  }

  pid_ = -1;
  return WIFEXITED(status) ? SubprocessWaitResult::Exited(WEXITSTATUS(status))
                           : SubprocessWaitResult::Failed();
}

template <>
Subprocess<SubprocessMode::kSignalSafe>::~Subprocess() {
  Reset();
}

#  endif  // HAVE__FORK and HAVE_EXECV

}  // namespace tools
}  // namespace nglog

#endif  // HAVE_SUBPROCESS and POSIX fork support
