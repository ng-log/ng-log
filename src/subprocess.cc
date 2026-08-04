// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "subprocess.h"

#include "utilities.h"

#ifdef HAVE_SUBPROCESS

#  include <algorithm>
#  include <cerrno>
#  include <limits>
#  include <thread>
#  include <utility>

#  ifdef NGLOG_OS_WINDOWS

#    include <atomic>
#    include <cstring>
#    include <cwchar>
#    include <limits>
#    include <locale>
#    include <memory>
#    include <string>
#    include <type_traits>

#    include "internal/utf8.h"

namespace nglog {
inline namespace tools {

namespace {

// CreateProcessW() takes a single, already-quoted command line rather
// than an argv[] array. This quotes and appends |arg| to |out| following
// the rules the Microsoft C runtime itself uses to later split that
// command line back into argv[], so that a round trip through
// CreateProcessW() reproduces |arg| exactly, including embedded spaces,
// quotes, or backslashes.
void AppendQuotedArg(const std::wstring& arg, std::wstring& out) {
  if (!out.empty()) {
    out += L' ';
  }

  if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    out += arg;
    return;
  }

  out += L'"';

  for (std::size_t i = 0; i <= arg.size(); ++i) {
    std::size_t num_backslashes = 0;

    while (i < arg.size() && arg[i] == L'\\') {
      ++num_backslashes;
      ++i;
    }

    if (i == arg.size()) {
      out.append(num_backslashes * 2, L'\\');
      break;
    }

    if (arg[i] == L'"') {
      out.append(num_backslashes * 2 + 1, L'\\');
      out += arg[i];
    } else {
      out.append(num_backslashes, L'\\');
      out += arg[i];
    }
  }

  out += L'"';
}

// True if |envp| already carries its own "PATH=" entry, case-insensitively
// as Windows environment variable names are. |locale| is expected to be
// std::locale::classic(), passed in rather than looked up on every call.
bool HasPathEntry(char* const envp[], const std::locale& locale) {
  constexpr char kPathPrefix[] = "PATH=";
  constexpr std::size_t kPathPrefixLen = sizeof(kPathPrefix) - 1;

  for (char* const* entry = envp; *entry != nullptr; ++entry) {
    const char* p = *entry;

    if (std::strlen(p) < kPathPrefixLen) {
      continue;  // Entry too short to be "PATH=...".
    }

    char upper[kPathPrefixLen];
    std::transform(p, p + kPathPrefixLen, upper,
                   [&locale](char c) { return std::toupper(c, locale); });

    if (std::search(upper, upper + kPathPrefixLen, kPathPrefix,
                    kPathPrefix + kPathPrefixLen) == upper) {
      return true;
    }
  }
  return false;
}

// CreateProcessW() also takes its environment block, when one is given
// at all, as one buffer of NUL-separated "NAME=VALUE" strings terminated
// by an extra NUL, rather than an envp[] array.
//
// Without a PATH entry of its own, the child cannot resolve a bare
// argv[0] (e.g. "addr2line") via CreateProcessW()'s own implicit search,
// which looks at the *new* process' environment rather than this one's.
// Carrying this process' own PATH through, even when the caller
// otherwise wants a minimal child environment, matches what
// posix_spawnp()/execvp() already guarantee on the POSIX side: argv[0]
// resolves via the caller's PATH regardless of what envp the child
// itself ends up running with.
std::wstring BuildEnvironmentBlock(char* const envp[]) {
  std::wstring block;

  for (char* const* entry = envp; *entry != nullptr; ++entry) {
    for (const char* p = *entry; *p != '\0'; ++p) {
      block += static_cast<wchar_t>(static_cast<unsigned char>(*p));
    }

    block += L'\0';
  }

  if (!HasPathEntry(envp, std::locale::classic())) {
    const DWORD needed = GetEnvironmentVariableW(L"PATH", nullptr, 0);

    if (needed > 0) {
      std::wstring path(needed, L'\0');
      const DWORD written = GetEnvironmentVariableW(L"PATH", &path[0], needed);

      if (written > 0 && written < needed) {
        path.resize(written);
        block += L"PATH=";
        block += path;
        block += L'\0';
      }
    }
  }

  block += L'\0';
  return block;
}

bool ResolveExecutablePath(const char* name, std::wstring* resolved_path) {
  std::wstring wide_name;
  if (!internal::Utf8ToWide(name, std::strlen(name), &wide_name)) {
    return false;
  }

  if (std::strpbrk(name, "\\/") != nullptr) {
    *resolved_path = std::move(wide_name);
    return true;
  }

  wchar_t buffer[MAX_PATH];
  const DWORD length = SearchPathW(nullptr, wide_name.c_str(), L".exe",
                                   MAX_PATH, buffer, nullptr);
  if (length == 0 || length >= MAX_PATH) {
    *resolved_path = std::move(wide_name);
    return true;
  }
  resolved_path->assign(buffer, length);
  return true;
}

DWORD ClampTimeoutMillis(std::chrono::milliseconds timeout) {
  using Millis = std::chrono::milliseconds;
  constexpr auto kMax = Millis{static_cast<Millis::rep>(INFINITE - 1)};
  return static_cast<DWORD>(
      std::max(Millis::zero(), std::min(timeout, kMax)).count());
}

constexpr DWORD kPipeBufferSize = 4096;

// Anonymous pipes created via CreatePipe() only support synchronous I/O,
// with no way to bound a WriteFile()/ReadFile() call by a timeout. Named
// pipes do not have that limitation: FILE_FLAG_OVERLAPPED lets the end
// this process keeps use overlapped (asynchronous) I/O, bounded by
// WaitForSingleObject() and CancelIoEx(), while the end handed to the
// child stays an ordinary synchronous handle it can read or write
// without knowing anything unusual is going on.
//
// |server_is_reader| selects the direction of |our_end|: true for the
// process' stdout (this process reads, the child writes), false for its
// stdin (this process writes, the child reads).
bool CreateOverlappedPipePair(bool server_is_reader, UniqueHandle& our_end,
                              UniqueHandle& child_end) {
  static std::atomic<unsigned long> counter{0};
  constexpr std::size_t kNameLength = 64;
  wchar_t name[kNameLength];
  const int written =
      std::swprintf(name, kNameLength, L"\\\\.\\pipe\\nglog-subprocess-%lu-%lu",
                    static_cast<unsigned long>(GetCurrentProcessId()),
                    counter.fetch_add(1, std::memory_order_relaxed));

  if (written < 0) {
    return false;
  }

  const DWORD open_mode =
      (server_is_reader ? PIPE_ACCESS_INBOUND : PIPE_ACCESS_OUTBOUND) |
      FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE;

  HANDLE our_handle =
      CreateNamedPipeW(name, open_mode, PIPE_TYPE_BYTE | PIPE_WAIT, 1,
                       kPipeBufferSize, kPipeBufferSize, 0, nullptr);

  if (our_handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  our_end.reset(our_handle);

  SECURITY_ATTRIBUTES inheritable{};
  inheritable.nLength = sizeof(inheritable);
  inheritable.bInheritHandle = TRUE;

  const DWORD child_access = server_is_reader ? GENERIC_WRITE : GENERIC_READ;
  HANDLE child_handle =
      CreateFileW(name, child_access, 0, &inheritable, OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL, nullptr);

  if (child_handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  child_end.reset(child_handle);

  return true;
}

// Runs a WriteFile()/ReadFile()-shaped |io| call on |handle| through
// overlapped I/O, waiting at most |timeout| for it to complete.
// |io(overlapped)| must return the same BOOL that WriteFile()/ReadFile()
// itself would.
template <class Functor>
DWORD RunOverlapped(HANDLE handle, std::chrono::milliseconds timeout,
                    Functor io) {
  UniqueHandle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};

  if (!event) {
    return 0;
  }

  OVERLAPPED overlapped{};
  overlapped.hEvent = event.get();

  const BOOL immediate = io(overlapped);

  if (!immediate && GetLastError() != ERROR_IO_PENDING) {
    return 0;
  }

  if (!immediate &&
      WaitForSingleObject(overlapped.hEvent, ClampTimeoutMillis(timeout)) !=
          WAIT_OBJECT_0) {
    // Timed out: cancel the pending operation, then wait for it to
    // actually finish (successfully or as cancelled) before the
    // OVERLAPPED structure goes out of scope. Any bytes transferred
    // before the cancel are discarded, matching the documented "0 on
    // timeout" contract.
    DWORD ignored = 0;
    CancelIoEx(handle, &overlapped);
    GetOverlappedResult(handle, &overlapped, &ignored, TRUE);
    return 0;
  }

  // Even after an immediate success, GetOverlappedResult() is the
  // documented way to retrieve the transfer count.
  DWORD transferred = 0;
  GetOverlappedResult(handle, &overlapped, &transferred, FALSE);
  return transferred;
}

}  // namespace

Subprocess::Subprocess(Subprocess&& other) noexcept
    : process_{std::move(other.process_)},
      stdin_write_{std::move(other.stdin_write_)},
      stdout_read_{std::move(other.stdout_read_)} {}

Subprocess& Subprocess::operator=(Subprocess&& other) noexcept {
  if (this != &other) {
    Reset();
    process_ = std::move(other.process_);
    stdin_write_ = std::move(other.stdin_write_);
    stdout_read_ = std::move(other.stdout_read_);
  }

  return *this;
}

void Subprocess::Reset() noexcept {
  if (process_) {
    TerminateProcess(process_.get(), 1);
    WaitForSingleObject(process_.get(), INFINITE);
    process_.reset();
  }

  CloseStdin();
  stdout_read_.reset();
}

bool Subprocess::Spawn(char* const argv[], char* const envp[]) {
  Reset();

  if (argv == nullptr || argv[0] == nullptr || envp == nullptr) {
    return false;
  }

  std::wstring executable_path;
  if (!ResolveExecutablePath(argv[0], &executable_path)) {
    return false;
  }

  std::wstring command_line;
  for (char* const* arg = argv; *arg != nullptr; ++arg) {
    std::wstring wide_arg;
    if (!internal::Utf8ToWide(*arg, std::strlen(*arg), &wide_arg)) {
      return false;
    }
    AppendQuotedArg(wide_arg, command_line);
  }

  std::wstring environment_block = BuildEnvironmentBlock(envp);

  UniqueHandle stdin_write;
  UniqueHandle stdin_read;

  if (!CreateOverlappedPipePair(/*server_is_reader=*/false, stdin_write,
                                stdin_read)) {
    return false;
  }

  UniqueHandle stdout_read;
  UniqueHandle stdout_write;

  if (!CreateOverlappedPipePair(/*server_is_reader=*/true, stdout_read,
                                stdout_write)) {
    return false;
  }

  // Discards the child's stderr the same way the POSIX backend does:
  // redirected to a null device rather than inherited, so it cannot end
  // up interleaved with this process' own output.
  SECURITY_ATTRIBUTES inheritable{};
  inheritable.nLength = sizeof(inheritable);
  inheritable.bInheritHandle = TRUE;
  UniqueHandle null_device{CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE,
                                       &inheritable, OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL, nullptr)};

  STARTUPINFOW startup_info{};
  startup_info.cb = sizeof(startup_info);
  startup_info.dwFlags = STARTF_USESTDHANDLES;
  startup_info.hStdInput = stdin_read.get();
  startup_info.hStdOutput = stdout_write.get();
  startup_info.hStdError = null_device.get();

  PROCESS_INFORMATION process_info{};
  // CreateProcessW() requires mutable buffers for the command line and
  // environment block. std::wstring::data() only gained a non-const
  // overload in C++17, so &…[0] is used instead to stay compatible with
  // this project's C++14 baseline. Both strings are guaranteed
  // contiguous and NUL-terminated, including when empty.
  const BOOL spawned = CreateProcessW(
      executable_path.c_str(), &command_line[0], nullptr, nullptr,
      /*bInheritHandles=*/TRUE, CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
      &environment_block[0], nullptr, &startup_info, &process_info);
  const DWORD spawn_error = GetLastError();

  // These were only needed for the child to inherit. The child has its
  // own copies (or, on failure, nothing needs them any more).
  stdin_read.reset();
  stdout_write.reset();

  if (!spawned) {
    SetLastError(spawn_error);
    return false;
  }

  UniqueHandle thread{process_info.hThread};
  process_ = UniqueHandle{process_info.hProcess};
  stdin_write_ = std::move(stdin_write);
  stdout_read_ = std::move(stdout_read);
  return true;
}

Subprocess::operator bool() const noexcept { return process_ != nullptr; }

std::size_t Subprocess::WriteStdin(const char* data, std::size_t size,
                                   std::chrono::milliseconds timeout) {
  if (stdin_write_ == nullptr) {
    return 0;
  }

  // lpNumberOfBytesWritten is passed as nullptr, per Microsoft's
  // guidance for overlapped calls: its value while the write is still
  // pending is unreliable, and GetOverlappedResult() (inside
  // RunOverlapped()) is the documented way to retrieve the actual count.
  const DWORD written = RunOverlapped(
      stdin_write_.get(), timeout, [this, data, size](OVERLAPPED& overlapped) {
        return WriteFile(stdin_write_.get(), data,
                         static_cast<DWORD>(std::min<std::size_t>(
                             size, std::numeric_limits<DWORD>::max())),
                         nullptr, &overlapped);
      });

  return static_cast<std::size_t>(written);
}

void Subprocess::CloseStdin() { stdin_write_.reset(); }

std::size_t Subprocess::ReadStdout(char* out, std::size_t out_size,
                                   std::chrono::milliseconds timeout) {
  if (!stdout_read_ || out_size == 0) {
    return 0;
  }

  const DWORD read = RunOverlapped(
      stdout_read_.get(), timeout,
      [this, out, out_size](OVERLAPPED& overlapped) {
        return ReadFile(stdout_read_.get(), out,
                        static_cast<DWORD>(std::min<std::size_t>(
                            out_size, std::numeric_limits<DWORD>::max())),
                        nullptr, &overlapped);
      });

  return static_cast<std::size_t>(read);
}

void Subprocess::Wait(std::chrono::milliseconds timeout) {
  if (!process_) {
    return;
  }

  if (WaitForSingleObject(process_.get(), ClampTimeoutMillis(timeout)) ==
      WAIT_TIMEOUT) {
    TerminateProcess(process_.get(), 1);
    WaitForSingleObject(process_.get(), INFINITE);
  }

  process_.reset();
}

}  // namespace tools
}  // namespace nglog

#  else  // !NGLOG_OS_WINDOWS

#    include <fcntl.h>
#    include <poll.h>
#    include <signal.h>
#    include <sys/wait.h>
#    include <unistd.h>
#    ifdef HAVE_POSIX_SPAWN
#      include <spawn.h>
#    endif  // HAVE_POSIX_SPAWN

namespace nglog {
inline namespace tools {

namespace {

// Re-runs run() until it doesn't fail with EINTR.
template <class Functor>
auto FailureRetry(Functor run) noexcept(noexcept(run())) {
  decltype(run()) result;

  while ((result = run()) == -1 && errno == EINTR) {
  }

  return result;
}

// Marks |fd| close-on-exec, so it does not leak into a spawned child.
// Without this, the child inherits all four pipe descriptors instead of
// just its own stdin and stdout, including a copy of its own stdin
// pipe's write end, which makes it unable to ever see EOF on stdin.
// dup2() (used below to install the two ends the child does need)
// clears FD_CLOEXEC on the target descriptor regardless of the source's
// flags, so this does not affect the ends the child is meant to keep.
bool SetCloseOnExec(int fd) {
  const int flags = ::fcntl(fd, F_GETFD);
  return flags != -1 && ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != -1;
}

int ClampPollTimeout(std::chrono::milliseconds timeout) {
  using Milliseconds = std::chrono::milliseconds;
  constexpr Milliseconds::rep kMaximum =
      static_cast<Milliseconds::rep>(std::numeric_limits<int>::max());
  const Milliseconds::rep count = std::max<Milliseconds::rep>(
      0, std::min<Milliseconds::rep>(timeout.count(), kMaximum));
  return static_cast<int>(count);
}

#    ifdef HAVE_POSIX_SPAWN

class PosixSpawnFileActions final {
 public:
  PosixSpawnFileActions() noexcept
      : initialized_{posix_spawn_file_actions_init(&actions_) == 0} {}

  ~PosixSpawnFileActions() {
    if (initialized_) {
      posix_spawn_file_actions_destroy(&actions_);
    }
  }

  PosixSpawnFileActions(const PosixSpawnFileActions&) = delete;
  PosixSpawnFileActions& operator=(const PosixSpawnFileActions&) = delete;

  explicit operator bool() const noexcept { return initialized_; }
  posix_spawn_file_actions_t* get() noexcept { return &actions_; }

 private:
  posix_spawn_file_actions_t actions_{};
  bool initialized_;
};

// Preferred over fork()+execvp() where available: posix_spawn() lets the
// C library perform the requested redirections internally (via
// posix_spawn_file_actions_t) rather than running our own code in the
// fork-but-not-yet-exec window, which on most systems is both faster
// (glibc typically implements it with a vfork()-like clone(), avoiding a
// full address-space copy) and less prone to a forked child deadlocking
// on a libc lock that was already held at the moment the parent was
// interrupted. The "p" variant, rather than plain posix_spawn(), searches
// PATH for argv[0] the same way a shell would, so no separate lookup step
// is needed here.
pid_t SpawnProcess(char* const argv[], char* const envp[], int stdin_fd,
                   int stdout_fd, int /*exec_error_fd*/) {
  PosixSpawnFileActions file_actions;

  if (!file_actions) {
    return -1;
  }

  posix_spawn_file_actions_adddup2(file_actions.get(), stdin_fd, STDIN_FILENO);
  posix_spawn_file_actions_adddup2(file_actions.get(), stdout_fd,
                                   STDOUT_FILENO);
  posix_spawn_file_actions_addopen(file_actions.get(), STDERR_FILENO,
                                   "/dev/null", O_WRONLY, 0);

  pid_t pid = -1;
  const int result =
      posix_spawnp(&pid, argv[0], file_actions.get(), nullptr, argv, envp);

  return result == 0 ? pid : -1;
}

#    else  // !HAVE_POSIX_SPAWN

// execvp(), unlike execve(), searches PATH for argv[0] but also always
// inherits the caller's environment: there is no portable, PATH-searching
// exec that also accepts a custom environment. Callers that depend on a
// minimal environment (see addr2line.cc) do not get that hardening on
// this fallback path.
pid_t SpawnProcess(char* const argv[], char* const /*envp*/[], int stdin_fd,
                   int stdout_fd, int exec_error_fd) {
  const pid_t pid = fork();

  if (pid < 0) {
    return -1;
  }

  if (pid == 0) {
    if (dup2(stdin_fd, STDIN_FILENO) < 0 ||
        dup2(stdout_fd, STDOUT_FILENO) < 0) {
      const int error = errno;
      FailureRetry([exec_error_fd, error] {
        return ::write(exec_error_fd, &error, sizeof(error));
      });
      _exit(127);
    }

    const int dev_null = open("/dev/null", O_WRONLY);

    if (dev_null >= 0) {
      dup2(dev_null, STDERR_FILENO);
    }

    execvp(argv[0], argv);
    const int error = errno;
    FailureRetry([exec_error_fd, error] {
      return ::write(exec_error_fd, &error, sizeof(error));
    });
    _exit(127);  // Shell convention for "command not found".
  }

  return pid;
}

#    endif  // HAVE_POSIX_SPAWN

}  // namespace

Subprocess::Subprocess(Subprocess&& other) noexcept
    : pid_{std::exchange(other.pid_, -1)},
      stdin_write_{std::move(other.stdin_write_)},
      stdout_read_{std::move(other.stdout_read_)} {}

Subprocess& Subprocess::operator=(Subprocess&& other) noexcept {
  if (this != &other) {
    Reset();
    pid_ = std::exchange(other.pid_, -1);
    stdin_write_ = std::move(other.stdin_write_);
    stdout_read_ = std::move(other.stdout_read_);
  }

  return *this;
}

void Subprocess::Reset() noexcept {
  const pid_t pid = std::exchange(pid_, -1);

  if (pid >= 0) {
    ::kill(pid, SIGKILL);
    int status = 0;
    FailureRetry([pid, &status] { return ::waitpid(pid, &status, 0); });
  }

  CloseStdin();

  stdout_read_.reset();
}

bool Subprocess::Spawn(char* const argv[], char* const envp[]) {
  Reset();

  FileDescriptor stdin_read;
  FileDescriptor stdin_write;
  int stdin_fds[2];

  if (::pipe(stdin_fds) != 0) {
    return false;
  }
  stdin_read.reset(stdin_fds[0]);
  stdin_write.reset(stdin_fds[1]);

  FileDescriptor stdout_read;
  FileDescriptor stdout_write;
  int stdout_fds[2];

  if (::pipe(stdout_fds) != 0) {
    return false;
  }
  stdout_read.reset(stdout_fds[0]);
  stdout_write.reset(stdout_fds[1]);

  if (!SetCloseOnExec(stdin_read.get()) || !SetCloseOnExec(stdin_write.get()) ||
      !SetCloseOnExec(stdout_read.get()) ||
      !SetCloseOnExec(stdout_write.get())) {
    return false;
  }

#    ifndef HAVE_POSIX_SPAWN
  FileDescriptor exec_error_read;
  FileDescriptor exec_error_write;
  int exec_error_fds[2] = {-1, -1};
  if (::pipe(exec_error_fds) != 0) {
    return false;
  }
  exec_error_read.reset(exec_error_fds[0]);
  exec_error_write.reset(exec_error_fds[1]);
  if (!SetCloseOnExec(exec_error_read.get()) ||
      !SetCloseOnExec(exec_error_write.get())) {
    return false;
  }
  const int exec_error_fd = exec_error_write.get();
#    else
  constexpr int exec_error_fd = -1;
#    endif

  const pid_t pid = SpawnProcess(argv, envp, stdin_read.get(),
                                 stdout_write.get(), exec_error_fd);

  stdin_read.reset();
  stdout_write.reset();

#    ifndef HAVE_POSIX_SPAWN
  exec_error_write.reset();
#    endif

  if (pid < 0) {
    return false;
  }

#    ifndef HAVE_POSIX_SPAWN
  int exec_error = 0;
  const ssize_t exec_error_bytes =
      FailureRetry([&exec_error, &exec_error_read] {
        return ::read(exec_error_read.get(), &exec_error, sizeof(exec_error));
      });
  exec_error_read.reset();

  if (exec_error_bytes != 0) {
    ::kill(pid, SIGKILL);
    int status = 0;
    FailureRetry([pid, &status] { return ::waitpid(pid, &status, 0); });
    return false;
  }
#    endif

  pid_ = pid;
  stdin_write_ = std::move(stdin_write);
  stdout_read_ = std::move(stdout_read);
  return true;
}

Subprocess::operator bool() const noexcept { return pid_ >= 0; }

std::size_t Subprocess::WriteStdin(const char* data, std::size_t size,
                                   std::chrono::milliseconds timeout) {
  if (!stdin_write_) {
    return 0;
  }

  struct pollfd pfd{};
  pfd.fd = stdin_write_.get();
  pfd.events = POLLOUT;

  const int poll_result = FailureRetry(
      [&pfd, timeout] { return ::poll(&pfd, 1, ClampPollTimeout(timeout)); });

  if (poll_result <= 0) {
    return 0;
  }

  const ssize_t written = FailureRetry(
      [this, data, size] { return ::write(stdin_write_.get(), data, size); });

  return written > 0 ? static_cast<std::size_t>(written) : 0;
}

void Subprocess::CloseStdin() { stdin_write_.reset(); }

std::size_t Subprocess::ReadStdout(char* out, std::size_t out_size,
                                   std::chrono::milliseconds timeout) {
  if (!stdout_read_) {
    return 0;
  }

  struct pollfd pfd{};
  pfd.fd = stdout_read_.get();
  pfd.events = POLLIN;

  const int poll_result = FailureRetry(
      [&pfd, timeout] { return ::poll(&pfd, 1, ClampPollTimeout(timeout)); });

  if (poll_result <= 0) {
    return 0;
  }

  const ssize_t bytes_read = FailureRetry([this, out, out_size] {
    return ::read(stdout_read_.get(), out, out_size);
  });

  return bytes_read > 0 ? static_cast<std::size_t>(bytes_read) : 0;
}

void Subprocess::Wait(std::chrono::milliseconds timeout) {
  if (pid_ < 0) {
    return;
  }

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  int status = 0;
  bool exited = false;

  for (;;) {
    const pid_t result = FailureRetry(
        [this, &status] { return ::waitpid(pid_, &status, WNOHANG); });

    if (result == pid_) {
      exited = true;
      break;
    }

    if (std::chrono::steady_clock::now() >= deadline) {
      break;
    }

    // yield() would let this poll as fast as the scheduler allows,
    // burning a full core for however long the child takes to exit.
    // A short sleep bounds that cost at the price of up to 1ms of
    // added latency, which reaping a child does not need to avoid.
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }

  if (!exited) {
    ::kill(pid_, SIGKILL);
    FailureRetry([this, &status] { return ::waitpid(pid_, &status, 0); });
  }

  pid_ = -1;
}

}  // namespace tools
}  // namespace nglog

#  endif  // NGLOG_OS_WINDOWS

namespace nglog {
inline namespace tools {

// Identical on every platform: Reset() itself is what differs.
Subprocess::~Subprocess() { Reset(); }

}  // namespace tools
}  // namespace nglog

#endif  // HAVE_SUBPROCESS
