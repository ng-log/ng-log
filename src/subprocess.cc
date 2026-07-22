// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "subprocess.h"

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

namespace nglog {
inline namespace tools {

namespace {

// CreateProcessW() takes a single, already-quoted command line rather
// than an argv[] array. This quotes and appends |arg| to |out| following
// the rules the Microsoft C runtime itself uses to later split that
// command line back into argv[], so that a round trip through
// CreateProcessW() reproduces |arg| exactly, including embedded spaces,
// quotes, or backslashes.
void AppendQuotedArg(const char* arg, std::wstring& out) {
  if (!out.empty()) {
    out += L' ';
  }

  std::wstring warg;
  while (*arg != '\0') {
    warg += static_cast<wchar_t>(static_cast<unsigned char>(*arg));
    ++arg;
  }

  if (!warg.empty() && warg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    out += warg;
    return;
  }

  out += L'"';

  for (std::size_t i = 0; i <= warg.size(); ++i) {
    std::size_t num_backslashes = 0;

    while (i < warg.size() && warg[i] == L'\\') {
      ++num_backslashes;
      ++i;
    }

    if (i == warg.size()) {
      out.append(num_backslashes * 2, L'\\');
      break;
    }

    if (warg[i] == L'"') {
      out.append(num_backslashes * 2 + 1, L'\\');
      out += warg[i];
    } else {
      out.append(num_backslashes, L'\\');
      out += warg[i];
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

// Resolves a bare program name (one with no path separator) to a full
// path, searching the calling process' own directory, its current
// directory, the system directories, and its own PATH environment
// variable, in that order. Returns |name| unchanged, path separators and
// all, if it already looks like a path, or if the search below fails to
// find it.
//
// CreateProcessW() can do this resolution itself when left with a NULL
// lpApplicationName, but it then searches PATH from the environment
// block handed to the *new* process rather than this one's own, which
// would silently defeat RunAddr2Line()'s deliberately minimal child
// environment: with no PATH entry to search, argv[0] alone would never
// resolve.
std::string ResolveExecutablePath(const char* name) {
  if (std::strpbrk(name, "\\/") != nullptr) {
    return name;
  }

  std::wstring wide_name;
  for (const char* p = name; *p != '\0'; ++p) {
    wide_name += static_cast<wchar_t>(static_cast<unsigned char>(*p));
  }

  wchar_t buffer[MAX_PATH];
  const DWORD length = SearchPathW(
      nullptr, wide_name.c_str(), L".exe",
      static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])), buffer, nullptr);

  if (length == 0 || length >= sizeof(buffer) / sizeof(buffer[0])) {
    return name;
  }

  std::string resolved;
  resolved.reserve(length);
  for (DWORD i = 0; i < length; ++i) {
    resolved += static_cast<char>(buffer[i]);
  }
  return resolved;
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
bool CreateOverlappedPipePair(bool server_is_reader, HANDLE& our_end,
                              HANDLE& child_end) {
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

  our_end = CreateNamedPipeW(name, open_mode, PIPE_TYPE_BYTE | PIPE_WAIT, 1,
                             kPipeBufferSize, kPipeBufferSize, 0, nullptr);

  if (our_end == INVALID_HANDLE_VALUE) {
    return false;
  }

  SECURITY_ATTRIBUTES inheritable{};
  inheritable.nLength = sizeof(inheritable);
  inheritable.bInheritHandle = TRUE;

  const DWORD child_access = server_is_reader ? GENERIC_WRITE : GENERIC_READ;
  child_end = CreateFileW(name, child_access, 0, &inheritable, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, nullptr);

  if (child_end == INVALID_HANDLE_VALUE) {
    CloseHandle(our_end);
    return false;
  }

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

  HANDLE stdin_write = nullptr;
  HANDLE stdin_read = nullptr;

  if (!CreateOverlappedPipePair(/*server_is_reader=*/false, stdin_write,
                                stdin_read)) {
    return false;
  }

  HANDLE stdout_read = nullptr;
  HANDLE stdout_write = nullptr;

  if (!CreateOverlappedPipePair(/*server_is_reader=*/true, stdout_read,
                                stdout_write)) {
    CloseHandle(stdin_write);
    CloseHandle(stdin_read);
    return false;
  }

  // Discards the child's stderr the same way the POSIX backend does:
  // redirected to a null device rather than inherited, so it cannot end
  // up interleaved with this process' own output.
  SECURITY_ATTRIBUTES inheritable{};
  inheritable.nLength = sizeof(inheritable);
  inheritable.bInheritHandle = TRUE;
  HANDLE null_device =
      CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &inheritable,
                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

  std::wstring command_line;
  if (argv[0] != nullptr) {
    const std::string resolved = ResolveExecutablePath(argv[0]);
    AppendQuotedArg(resolved.c_str(), command_line);
  }
  for (char* const* arg = argv + 1; *arg != nullptr; ++arg) {
    AppendQuotedArg(*arg, command_line);
  }

  std::wstring environment_block = BuildEnvironmentBlock(envp);

  STARTUPINFOW startup_info{};
  startup_info.cb = sizeof(startup_info);
  startup_info.dwFlags = STARTF_USESTDHANDLES;
  startup_info.hStdInput = stdin_read;
  startup_info.hStdOutput = stdout_write;
  startup_info.hStdError = null_device;

  PROCESS_INFORMATION process_info{};
  // CreateProcessW() requires mutable buffers for the command line and
  // environment block. std::wstring::data() only gained a non-const
  // overload in C++17, so &…[0] is used instead to stay compatible with
  // this project's C++14 baseline. Both strings are guaranteed
  // contiguous and NUL-terminated, including when empty.
  const BOOL spawned = CreateProcessW(
      nullptr, &command_line[0], nullptr, nullptr,
      /*bInheritHandles=*/TRUE, CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
      &environment_block[0], nullptr, &startup_info, &process_info);

  // These were only needed for the child to inherit. The child has its
  // own copies (or, on failure, nothing needs them any more).
  CloseHandle(stdin_read);
  CloseHandle(stdout_write);

  if (null_device != INVALID_HANDLE_VALUE) {
    CloseHandle(null_device);
  }

  if (!spawned) {
    CloseHandle(stdin_write);
    CloseHandle(stdout_read);
    return false;
  }

  CloseHandle(process_info.hThread);
  process_ = UniqueHandle{process_info.hProcess};
  stdin_write_ = UniqueHandle{stdin_write};
  stdout_read_ = UniqueHandle{stdout_read};
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
  posix_spawn_file_actions_t file_actions;

  if (posix_spawn_file_actions_init(&file_actions) != 0) {
    return -1;
  }

  posix_spawn_file_actions_adddup2(&file_actions, stdin_fd, STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&file_actions, stdout_fd, STDOUT_FILENO);
  posix_spawn_file_actions_addopen(&file_actions, STDERR_FILENO, "/dev/null",
                                   O_WRONLY, 0);

  pid_t pid = -1;
  const int result =
      posix_spawnp(&pid, argv[0], &file_actions, nullptr, argv, envp);
  posix_spawn_file_actions_destroy(&file_actions);

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
      stdin_write_{std::exchange(other.stdin_write_, -1)},
      stdout_read_{std::exchange(other.stdout_read_, -1)} {}

Subprocess& Subprocess::operator=(Subprocess&& other) noexcept {
  if (this != &other) {
    Reset();
    pid_ = std::exchange(other.pid_, -1);
    stdin_write_ = std::exchange(other.stdin_write_, -1);
    stdout_read_ = std::exchange(other.stdout_read_, -1);
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

  const int stdout_read = std::exchange(stdout_read_, -1);

  if (stdout_read >= 0) {
    ::close(stdout_read);
  }
}

bool Subprocess::Spawn(char* const argv[], char* const envp[]) {
  Reset();

  int stdin_fds[2];

  if (::pipe(stdin_fds) != 0) {
    return false;
  }

  int stdout_fds[2];

  if (::pipe(stdout_fds) != 0) {
    ::close(stdin_fds[0]);
    ::close(stdin_fds[1]);
    return false;
  }

  if (!SetCloseOnExec(stdin_fds[0]) || !SetCloseOnExec(stdin_fds[1]) ||
      !SetCloseOnExec(stdout_fds[0]) || !SetCloseOnExec(stdout_fds[1])) {
    ::close(stdin_fds[0]);
    ::close(stdin_fds[1]);
    ::close(stdout_fds[0]);
    ::close(stdout_fds[1]);
    return false;
  }

#    ifndef HAVE_POSIX_SPAWN
  int exec_error_fds[2] = {-1, -1};
  if (::pipe(exec_error_fds) != 0 || !SetCloseOnExec(exec_error_fds[0]) ||
      !SetCloseOnExec(exec_error_fds[1])) {
    if (exec_error_fds[0] >= 0) {
      ::close(exec_error_fds[0]);
    }
    if (exec_error_fds[1] >= 0) {
      ::close(exec_error_fds[1]);
    }
    ::close(stdin_fds[1]);
    ::close(stdout_fds[0]);
    return false;
  }
  const int exec_error_fd = exec_error_fds[1];
#    else
  constexpr int exec_error_fd = -1;
#    endif

  const pid_t pid =
      SpawnProcess(argv, envp, stdin_fds[0], stdout_fds[1], exec_error_fd);

  ::close(stdin_fds[0]);
  ::close(stdout_fds[1]);

#    ifndef HAVE_POSIX_SPAWN
  ::close(exec_error_fds[1]);
#    endif

  if (pid < 0) {
    ::close(stdin_fds[1]);
    ::close(stdout_fds[0]);
#    ifndef HAVE_POSIX_SPAWN
    ::close(exec_error_fds[0]);
#    endif
    return false;
  }

#    ifndef HAVE_POSIX_SPAWN
  int exec_error = 0;
  const ssize_t exec_error_bytes = FailureRetry([&exec_error, &exec_error_fds] {
    return ::read(exec_error_fds[0], &exec_error, sizeof(exec_error));
  });
  ::close(exec_error_fds[0]);

  if (exec_error_bytes != 0) {
    ::kill(pid, SIGKILL);
    int status = 0;
    FailureRetry([pid, &status] { return ::waitpid(pid, &status, 0); });
    ::close(stdin_fds[1]);
    ::close(stdout_fds[0]);
    return false;
  }
#    endif

  pid_ = pid;
  stdin_write_ = stdin_fds[1];
  stdout_read_ = stdout_fds[0];
  return true;
}

Subprocess::operator bool() const noexcept { return pid_ >= 0; }

std::size_t Subprocess::WriteStdin(const char* data, std::size_t size,
                                   std::chrono::milliseconds timeout) {
  if (stdin_write_ < 0) {
    return 0;
  }

  struct pollfd pfd{};
  pfd.fd = stdin_write_;
  pfd.events = POLLOUT;

  const int poll_result = FailureRetry(
      [&pfd, timeout] { return ::poll(&pfd, 1, ClampPollTimeout(timeout)); });

  if (poll_result <= 0) {
    return 0;
  }

  const ssize_t written = FailureRetry(
      [this, data, size] { return ::write(stdin_write_, data, size); });

  return written > 0 ? static_cast<std::size_t>(written) : 0;
}

void Subprocess::CloseStdin() {
  const int stdin_write = std::exchange(stdin_write_, -1);

  if (stdin_write >= 0) {
    ::close(stdin_write);
  }
}

std::size_t Subprocess::ReadStdout(char* out, std::size_t out_size,
                                   std::chrono::milliseconds timeout) {
  if (stdout_read_ < 0) {
    return 0;
  }

  struct pollfd pfd{};
  pfd.fd = stdout_read_;
  pfd.events = POLLIN;

  const int poll_result = FailureRetry(
      [&pfd, timeout] { return ::poll(&pfd, 1, ClampPollTimeout(timeout)); });

  if (poll_result <= 0) {
    return 0;
  }

  const ssize_t bytes_read = FailureRetry(
      [this, out, out_size] { return ::read(stdout_read_, out, out_size); });

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
