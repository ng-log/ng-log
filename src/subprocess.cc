// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "subprocess.h"

#include "config.h"
#include "ng-log/platform.h"
#include "utilities.h"

// The normal mode uses posix_spawn() where available and falls back to
// fork() and exec() otherwise. The signal-safe mode is defined separately in
// subprocess_fork.cc, so that the normal implementation cannot enter a fork
// child that is subject to the constraints of signal handlers.

#ifdef HAVE_SUBPROCESS

#  include <algorithm>
#  include <cerrno>
#  include <limits>
#  include <thread>
#  include <utility>

#  ifdef NGLOG_OS_WINDOWS

#    include <array>
#    include <atomic>
#    include <cstring>
#    include <cwchar>
#    include <initializer_list>
#    include <limits>
#    include <locale>
#    include <memory>
#    include <string>
#    include <type_traits>
#    include <vector>

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

    if (std::equal(upper, upper + kPathPrefixLen, kPathPrefix)) {
      return true;
    }
  }
  return false;
}

// CreateProcessW() also takes its environment block, when one is given
// at all, as one buffer of NUL-separated "NAME=VALUE" strings terminated
// by an extra NUL, rather than an envp[] array.
//
// A block without a PATH entry receives the PATH of this process, so that
// the child finds the programs it starts in turn even when the caller asks
// for an otherwise minimal environment. The executable of the child itself
// is resolved by this process beforehand, see ResolveExecutablePath().
// Entries are UTF-8 like the arguments. Returns false for an entry that is
// not valid UTF-8.
bool BuildEnvironmentBlock(char* const envp[], std::wstring* environment) {
  std::wstring& block = *environment;
  block.clear();

  for (char* const* entry = envp; *entry != nullptr; ++entry) {
    std::wstring wide_entry;
    if (!internal::Utf8ToWide(*entry, std::strlen(*entry), &wide_entry)) {
      return false;
    }
    block += wide_entry;
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
  return true;
}

constexpr wchar_t kExecutableExtension[] = L".exe";

// Mirrors CreateProcessW(), which appends .exe to a program name without an
// extension only when the name is passed on the command line.
void AppendDefaultExtension(std::wstring* path) {
  const std::size_t name_start = path->find_last_of(L"\\/");
  const std::size_t extension_start = path->find_last_of(L'.');
  if (extension_start == std::wstring::npos ||
      (name_start != std::wstring::npos && extension_start < name_start)) {
    *path += kExecutableExtension;
  }
}

// Returns the directory that contains the executable of this process.
bool GetApplicationDirectory(std::wstring* directory) {
  std::vector<wchar_t> buffer(MAX_PATH);
  while (true) {
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    if (length == 0) {
      return false;
    }
    if (length < buffer.size()) {
      directory->assign(buffer.data(), length);
      const std::size_t separator = directory->find_last_of(L"\\/");
      if (separator == std::wstring::npos) {
        return false;
      }
      directory->resize(separator);
      return true;
    }
    constexpr std::size_t kGrowthFactor = 2;
    buffer.resize(buffer.size() * kGrowthFactor);
  }
}

// Searches the directory of this process' executable and the system
// directory. Unlike the default search order of SearchPathW(), it leaves out
// the current directory, from which a planted program would run instead.
bool SearchExecutable(const std::wstring& name, std::wstring* executable_path) {
  std::wstring search_path;
  if (!GetApplicationDirectory(&search_path)) {
    return false;
  }
  std::array<wchar_t, MAX_PATH> system_directory = {};
  const UINT system_length = GetSystemDirectoryW(
      system_directory.data(), static_cast<UINT>(system_directory.size()));
  if (system_length > 0 && system_length < system_directory.size()) {
    search_path += L';';
    search_path.append(system_directory.data(), system_length);
  }

  std::vector<wchar_t> buffer(MAX_PATH);
  while (true) {
    const DWORD length =
        SearchPathW(search_path.c_str(), name.c_str(), kExecutableExtension,
                    static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (length == 0) {
      return false;
    }
    if (length < buffer.size()) {
      return internal::PrepareWindowsPath(buffer.data(), length,
                                          executable_path);
    }
    // The returned length includes the terminating null character.
    buffer.resize(length);
  }
}

// Returns whether a PATH entry is a drive or UNC path rather than one relative
// to the current directory.
bool IsAbsoluteWindowsPath(const std::wstring& path) {
  constexpr std::size_t kUncPrefixLength = 2;
  constexpr std::size_t kDrivePrefixLength = 3;
  const auto is_separator = [](wchar_t character) {
    return character == L'\\' || character == L'/';
  };
  if (path.size() >= kUncPrefixLength && is_separator(path[0]) &&
      is_separator(path[1])) {
    return true;
  }
  return path.size() >= kDrivePrefixLength && path[1] == L':' &&
         is_separator(path[2]);
}

// Searches the absolute entries of PATH, including those whose resulting path
// exceeds MAX_PATH.
bool FindExecutableInPath(const std::wstring& name,
                          std::wstring* executable_path) {
  const DWORD required_length = GetEnvironmentVariableW(L"PATH", nullptr, 0);
  if (required_length == 0) {
    return false;
  }

  std::vector<wchar_t> buffer(required_length);
  const DWORD length = GetEnvironmentVariableW(
      L"PATH", buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length == 0 || length >= buffer.size()) {
    return false;
  }

  std::wstring path(buffer.data(), length);
  std::size_t entry_begin = 0;
  constexpr std::size_t kSingleCharacterLength = 1;
  constexpr std::size_t kQuotePairLength = 2;
  while (entry_begin <= path.size()) {
    const std::size_t entry_end = path.find(L';', entry_begin);
    const std::size_t entry_length = entry_end == std::wstring::npos
                                         ? std::wstring::npos
                                         : entry_end - entry_begin;
    std::wstring entry = path.substr(entry_begin, entry_length);
    if (entry.size() >= kQuotePairLength && entry.front() == L'"' &&
        entry.back() == L'"') {
      entry =
          entry.substr(kSingleCharacterLength, entry.size() - kQuotePairLength);
    }

    // Relative entries refer to the current directory.
    if (IsAbsoluteWindowsPath(entry)) {
      std::wstring candidate = std::move(entry);
      if (candidate.back() != L'\\' && candidate.back() != L'/') {
        candidate += L'\\';
      }
      candidate += name;
      AppendDefaultExtension(&candidate);

      std::wstring prepared_candidate;
      if (internal::PrepareWindowsPath(candidate.data(), candidate.size(),
                                       &prepared_candidate)) {
        const DWORD attributes = GetFileAttributesW(prepared_candidate.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
          *executable_path = std::move(prepared_candidate);
          return true;
        }
      }
    }

    if (entry_end == std::wstring::npos) {
      break;
    }
    entry_begin = entry_end + kSingleCharacterLength;
  }
  return false;
}

// Resolves a program name without a path separator using this process'
// search order rather than leaving the lookup to CreateProcessW(). The
// latter searches PATH from the environment block handed to the new
// process, which would defeat a deliberately minimal child environment, and
// the current directory, from which a planted program would run. Fails if
// the search finds nothing.
bool ResolveExecutablePath(const char* name, std::wstring* resolved_path) {
  std::wstring wide_name;
  if (!internal::Utf8ToWide(name, std::strlen(name), &wide_name)) {
    return false;
  }

  if (std::strpbrk(name, "\\/") != nullptr) {
    AppendDefaultExtension(&wide_name);
    return internal::PrepareWindowsPath(wide_name.data(), wide_name.size(),
                                        resolved_path);
  }

  return SearchExecutable(wide_name, resolved_path) ||
         FindExecutableInPath(wide_name, resolved_path);
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
    // actually finish before the OVERLAPPED structure goes out of scope.
    // The operation may still complete before the cancellation takes
    // effect. The bytes it transferred are then already consumed from or
    // committed to the pipe and must be reported instead of being lost.
    CancelIoEx(handle, &overlapped);
    DWORD transferred = 0;
    if (!GetOverlappedResult(handle, &overlapped, &transferred, TRUE)) {
      return 0;
    }
    return transferred;
  }

  // Even after an immediate success, GetOverlappedResult() is the
  // documented way to retrieve the transfer count.
  DWORD transferred = 0;
  GetOverlappedResult(handle, &overlapped, &transferred, FALSE);
  return transferred;
}

}  // namespace

template <>
Subprocess<SubprocessMode::kNormal>::Subprocess(Subprocess&& other) noexcept;
template <>
Subprocess<SubprocessMode::kNormal>&
Subprocess<SubprocessMode::kNormal>::operator=(Subprocess&& other) noexcept;
template <>
void Subprocess<SubprocessMode::kNormal>::Reset() noexcept;
template <>
bool Subprocess<SubprocessMode::kNormal>::Spawn(char* const argv[],
                                                char* const envp[]);
template <>
Subprocess<SubprocessMode::kNormal>::operator bool() const noexcept;
template <>
std::size_t Subprocess<SubprocessMode::kNormal>::WriteStdin(
    const char* data, std::size_t size,
    std::chrono::milliseconds timeout) noexcept;
template <>
void Subprocess<SubprocessMode::kNormal>::CloseStdin() noexcept;
template <>
std::size_t Subprocess<SubprocessMode::kNormal>::ReadStdout(
    char* out, std::size_t out_size,
    std::chrono::milliseconds timeout) noexcept;
template <>
SubprocessWaitResult Subprocess<SubprocessMode::kNormal>::Wait(
    std::chrono::milliseconds timeout) noexcept;

template <>
Subprocess<SubprocessMode::kNormal>::Subprocess(Subprocess&& other) noexcept
    : process_{std::move(other.process_)},
      stdin_write_{std::move(other.stdin_write_)},
      stdout_read_{std::move(other.stdout_read_)} {}

template <>
Subprocess<SubprocessMode::kNormal>&
Subprocess<SubprocessMode::kNormal>::operator=(Subprocess&& other) noexcept {
  if (this != &other) {
    Reset();
    process_ = std::move(other.process_);
    stdin_write_ = std::move(other.stdin_write_);
    stdout_read_ = std::move(other.stdout_read_);
  }

  return *this;
}

template <>
void Subprocess<SubprocessMode::kNormal>::Reset() noexcept {
  if (process_) {
    TerminateProcess(process_.get(), 1);
    WaitForSingleObject(process_.get(), INFINITE);
    process_.reset();
  }

  CloseStdin();
  stdout_read_.reset();
}

template <>
bool Subprocess<SubprocessMode::kNormal>::Spawn(char* const argv[],
                                                char* const envp[]) {
  Reset();

  if (argv == nullptr || argv[0] == nullptr || envp == nullptr) {
    return false;
  }

  std::wstring executable_path;
  if (!ResolveExecutablePath(argv[0], &executable_path)) {
    return false;
  }

  std::wstring executable_argument;
  if (!internal::Utf8ToWide(argv[0], std::strlen(argv[0]),
                            &executable_argument)) {
    return false;
  }

  std::wstring command_line;
  AppendQuotedArg(executable_argument, command_line);
  for (char* const* arg = argv + 1; *arg != nullptr; ++arg) {
    std::wstring wide_arg;
    if (!internal::Utf8ToWide(*arg, std::strlen(*arg), &wide_arg)) {
      return false;
    }
    AppendQuotedArg(wide_arg, command_line);
  }

  std::wstring environment_block;
  if (!BuildEnvironmentBlock(envp, &environment_block)) {
    return false;
  }

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

  // Without a list, the child would inherit every inheritable handle of this
  // process, including the pipe ends that other threads create for their own
  // children at the same time.
  std::vector<HANDLE> inherited_handles;
  for (const HANDLE handle :
       {stdin_read.get(), stdout_write.get(), null_device.get()}) {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
      inherited_handles.push_back(handle);
    }
  }
  constexpr DWORD kAttributeCount = 1;
  SIZE_T attribute_list_size = 0;
  InitializeProcThreadAttributeList(nullptr, kAttributeCount, 0,
                                    &attribute_list_size);
  std::vector<unsigned char> attribute_storage(attribute_list_size);
  auto* const attributes =
      reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
  if (!InitializeProcThreadAttributeList(attributes, kAttributeCount, 0,
                                         &attribute_list_size)) {
    return false;
  }
  struct AttributeListDeleter {
    LPPROC_THREAD_ATTRIBUTE_LIST list;
    ~AttributeListDeleter() { DeleteProcThreadAttributeList(list); }
  } const attribute_list_deleter{attributes};
  if (!UpdateProcThreadAttribute(
          attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
          inherited_handles.data(), inherited_handles.size() * sizeof(HANDLE),
          nullptr, nullptr)) {
    return false;
  }

  STARTUPINFOEXW startup_info{};
  startup_info.StartupInfo.cb = sizeof(startup_info);
  startup_info.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup_info.StartupInfo.hStdInput = stdin_read.get();
  startup_info.StartupInfo.hStdOutput = stdout_write.get();
  startup_info.StartupInfo.hStdError = null_device.get();
  startup_info.lpAttributeList = attributes;

  PROCESS_INFORMATION process_info{};
  // CreateProcessW() requires mutable buffers for the command line and
  // environment block. std::wstring::data() only gained a non-const
  // overload in C++17, so &…[0] is used instead to stay compatible with
  // this project's C++14 baseline. Both strings are guaranteed
  // contiguous and NUL-terminated, including when empty.
  const BOOL spawned = CreateProcessW(
      executable_path.c_str(), &command_line[0], nullptr, nullptr,
      /*bInheritHandles=*/TRUE,
      CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW |
          EXTENDED_STARTUPINFO_PRESENT,
      &environment_block[0], nullptr, &startup_info.StartupInfo, &process_info);
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

template <>
Subprocess<SubprocessMode::kNormal>::operator bool() const noexcept {
  return process_ != nullptr;
}

template <>
std::size_t Subprocess<SubprocessMode::kNormal>::WriteStdin(
    const char* data, std::size_t size,
    std::chrono::milliseconds timeout) noexcept {
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

template <>
void Subprocess<SubprocessMode::kNormal>::CloseStdin() noexcept {
  stdin_write_.reset();
}

template <>
std::size_t Subprocess<SubprocessMode::kNormal>::ReadStdout(
    char* out, std::size_t out_size,
    std::chrono::milliseconds timeout) noexcept {
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

template <>
SubprocessWaitResult Subprocess<SubprocessMode::kNormal>::Wait(
    std::chrono::milliseconds timeout) noexcept {
  if (!process_) {
    return SubprocessWaitResult::Failed();
  }

  const DWORD wait_result =
      WaitForSingleObject(process_.get(), ClampTimeoutMillis(timeout));
  if (wait_result == WAIT_TIMEOUT) {
    const BOOL terminated = TerminateProcess(process_.get(), 1);
    if (WaitForSingleObject(process_.get(), INFINITE) != WAIT_OBJECT_0) {
      process_.reset();
      return SubprocessWaitResult::Failed();
    }
    if (!terminated) {
      DWORD exit_code = STILL_ACTIVE;
      if (GetExitCodeProcess(process_.get(), &exit_code) == 0 ||
          exit_code == STILL_ACTIVE ||
          exit_code > static_cast<DWORD>(std::numeric_limits<int>::max())) {
        process_.reset();
        return SubprocessWaitResult::Failed();
      }
      process_.reset();
      return SubprocessWaitResult::Exited(static_cast<int>(exit_code));
    }
    process_.reset();
    return SubprocessWaitResult::TimedOut();
  } else if (wait_result != WAIT_OBJECT_0) {
    process_.reset();
    return SubprocessWaitResult::Failed();
  }

  DWORD exit_code = 0;
  if (GetExitCodeProcess(process_.get(), &exit_code) == 0 ||
      exit_code > static_cast<DWORD>(std::numeric_limits<int>::max())) {
    process_.reset();
    return SubprocessWaitResult::Failed();
  }
  process_.reset();
  return SubprocessWaitResult::Exited(static_cast<int>(exit_code));
}

}  // namespace tools
}  // namespace nglog

#  else  // !NGLOG_OS_WINDOWS

#    include <fcntl.h>
#    include <poll.h>
#    include <signal.h>  // IWYU pragma: keep
// IWYU pragma: no_include <csignal>
#    include <sys/wait.h>
#    include <unistd.h>

#    ifdef HAVE_POSIX_SPAWN
#      include <spawn.h>
#    else
#      include <cstdlib>

#      include "subprocess_fork.h"
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

#    ifndef HAVE_PIPE2
// Marks |fd| close-on-exec, so it does not leak into a spawned child.
bool SetCloseOnExec(int fd) {
  const int flags = ::fcntl(fd, F_GETFD);
  return flags != -1 && ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != -1;
}
#    endif

// Creates a pipe whose ends are close-on-exec. Without this, the child
// inherits all four pipe descriptors instead of just its own stdin and stdout,
// including a copy of its own stdin pipe's write end, which makes it unable
// to ever see EOF on stdin. Setting the flag after creation would leak the
// ends into programs that other threads start in between. dup2(), used to
// install the two ends the child does need, clears FD_CLOEXEC on the target
// descriptor, so this does not affect the ends the child is meant to keep.
bool CreateCloseOnExecPipe(FileDescriptor* read_end,
                           FileDescriptor* write_end) {
  int fds[2];
#    ifdef HAVE_PIPE2
  if (::pipe2(fds, O_CLOEXEC) != 0) {
    return false;
  }
  read_end->reset(fds[0]);
  write_end->reset(fds[1]);
  return true;
#    else
  if (::pipe(fds) != 0) {
    return false;
  }
  read_end->reset(fds[0]);
  write_end->reset(fds[1]);
  return SetCloseOnExec(fds[0]) && SetCloseOnExec(fds[1]);
#    endif
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
pid_t SpawnProcessNormal(char* const argv[], char* const envp[], int stdin_fd,
                         int stdout_fd, int /*exec_error_fd*/) {
  PosixSpawnFileActions file_actions;

  if (!file_actions) {
    return -1;
  }

  // Without these actions, the child would use the streams of this process.
  if (posix_spawn_file_actions_adddup2(file_actions.get(), stdin_fd,
                                       STDIN_FILENO) != 0 ||
      posix_spawn_file_actions_adddup2(file_actions.get(), stdout_fd,
                                       STDOUT_FILENO) != 0 ||
      posix_spawn_file_actions_addopen(file_actions.get(), STDERR_FILENO,
                                       "/dev/null", O_WRONLY, 0) != 0) {
    return -1;
  }

  pid_t pid = -1;
  const int result =
      posix_spawnp(&pid, argv[0], file_actions.get(), nullptr, argv, envp);

  return result == 0 ? pid : -1;
}

#    else  // !HAVE_POSIX_SPAWN

pid_t SpawnProcessNormal(char* const argv[], char* const envp[], int stdin_fd,
                         int stdout_fd, int exec_error_fd) {
  // Searches PATH like posix_spawnp().
  return internal::SpawnProcessWithFork(argv, envp, std::getenv("PATH"),
                                        stdin_fd, stdout_fd, exec_error_fd,
                                        &fork, &internal::ExecuteWithExecvp);
}

#    endif  // HAVE_POSIX_SPAWN

}  // namespace

template <>
Subprocess<SubprocessMode::kNormal>::Subprocess(Subprocess&& other) noexcept;
template <>
Subprocess<SubprocessMode::kNormal>&
Subprocess<SubprocessMode::kNormal>::operator=(Subprocess&& other) noexcept;
template <>
void Subprocess<SubprocessMode::kNormal>::Reset() noexcept;
template <>
bool Subprocess<SubprocessMode::kNormal>::Spawn(char* const argv[],
                                                char* const envp[]) noexcept;
template <>
Subprocess<SubprocessMode::kNormal>::operator bool() const noexcept;
template <>
std::size_t Subprocess<SubprocessMode::kNormal>::WriteStdin(
    const char* data, std::size_t size,
    std::chrono::milliseconds timeout) noexcept;
template <>
void Subprocess<SubprocessMode::kNormal>::CloseStdin() noexcept;
template <>
std::size_t Subprocess<SubprocessMode::kNormal>::ReadStdout(
    char* out, std::size_t out_size,
    std::chrono::milliseconds timeout) noexcept;
template <>
SubprocessWaitResult Subprocess<SubprocessMode::kNormal>::Wait(
    std::chrono::milliseconds timeout) noexcept;

template <>
Subprocess<SubprocessMode::kNormal>::Subprocess(Subprocess&& other) noexcept
    : pid_{std::exchange(other.pid_, -1)},
      stdin_write_{std::move(other.stdin_write_)},
      stdout_read_{std::move(other.stdout_read_)} {}

template <>
Subprocess<SubprocessMode::kNormal>&
Subprocess<SubprocessMode::kNormal>::operator=(Subprocess&& other) noexcept {
  if (this != &other) {
    Reset();
    pid_ = std::exchange(other.pid_, -1);
    stdin_write_ = std::move(other.stdin_write_);
    stdout_read_ = std::move(other.stdout_read_);
  }

  return *this;
}

template <>
void Subprocess<SubprocessMode::kNormal>::Reset() noexcept {
  const pid_t pid = std::exchange(pid_, -1);

  if (pid >= 0) {
    ::kill(pid, SIGKILL);
    int status = 0;
    FailureRetry([pid, &status] { return ::waitpid(pid, &status, 0); });
  }

  CloseStdin();

  stdout_read_.reset();
}

template <>
bool Subprocess<SubprocessMode::kNormal>::Spawn(char* const argv[],
                                                char* const envp[]) noexcept {
  Reset();

  FileDescriptor stdin_read;
  FileDescriptor stdin_write;
  FileDescriptor stdout_read;
  FileDescriptor stdout_write;
  if (!CreateCloseOnExecPipe(&stdin_read, &stdin_write) ||
      !CreateCloseOnExecPipe(&stdout_read, &stdout_write)) {
    return false;
  }

#    ifndef HAVE_POSIX_SPAWN
  FileDescriptor exec_error_read;
  FileDescriptor exec_error_write;
  if (!CreateCloseOnExecPipe(&exec_error_read, &exec_error_write)) {
    return false;
  }
  const int exec_error_fd = exec_error_write.get();
#    else
  constexpr int exec_error_fd = -1;
#    endif

  const pid_t pid = SpawnProcessNormal(argv, envp, stdin_read.get(),
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

template <>
Subprocess<SubprocessMode::kNormal>::operator bool() const noexcept {
  return pid_ >= 0;
}

template <>
std::size_t Subprocess<SubprocessMode::kNormal>::WriteStdin(
    const char* data, std::size_t size,
    std::chrono::milliseconds timeout) noexcept {
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

template <>
void Subprocess<SubprocessMode::kNormal>::CloseStdin() noexcept {
  stdin_write_.reset();
}

template <>
std::size_t Subprocess<SubprocessMode::kNormal>::ReadStdout(
    char* out, std::size_t out_size,
    std::chrono::milliseconds timeout) noexcept {
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

template <>
SubprocessWaitResult Subprocess<SubprocessMode::kNormal>::Wait(
    std::chrono::milliseconds timeout) noexcept {
  if (pid_ < 0) {
    return SubprocessWaitResult::Failed();
  }

  // Saturates instead of overflowing for timeouts that stand for waiting
  // indefinitely, such as std::chrono::milliseconds::max(), and treats
  // negative timeouts as zero.
  const auto now = std::chrono::steady_clock::now();
  const auto longest_timeout =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::time_point::max() - now);
  const auto deadline = now + std::max(std::chrono::milliseconds::zero(),
                                       std::min(timeout, longest_timeout));
  int status = 0;
  bool exited = false;
  bool wait_failed = false;

  for (;;) {
    const pid_t result = FailureRetry(
        [this, &status] { return ::waitpid(pid_, &status, WNOHANG); });

    if (result == pid_) {
      exited = true;
      break;
    }

    if (result < 0) {
      if (errno == ECHILD) {
        // Another waiter reaped the process. Its ID may belong to another
        // process by now, so it must not be killed.
        pid_ = -1;
        return SubprocessWaitResult::Unavailable();
      }
      wait_failed = true;
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
    const pid_t pid = pid_;
    const int kill_result =
        FailureRetry([pid] { return ::kill(pid, SIGKILL); });
    const int kill_error = errno;
    const pid_t wait_result =
        FailureRetry([pid, &status] { return ::waitpid(pid, &status, 0); });
    const int wait_error = errno;
    pid_ = -1;

    if (wait_result != pid) {
      // With SIGCHLD ignored, the system reaps the killed child itself.
      return kill_result == 0 && wait_error == ECHILD
                 ? SubprocessWaitResult::TimedOut()
                 : SubprocessWaitResult::Failed();
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

}  // namespace tools
}  // namespace nglog

#  endif  // NGLOG_OS_WINDOWS

namespace nglog {
inline namespace tools {

// Identical on every platform: Reset() itself is what differs.
template <>
Subprocess<SubprocessMode::kNormal>::~Subprocess() {
  Reset();
}

}  // namespace tools
}  // namespace nglog

#endif  // HAVE_SUBPROCESS
