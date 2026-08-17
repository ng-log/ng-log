// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//
// A minimal, platform-independent way to run an external program and exchange a
// bounded amount of data with it, with caller-controlled timeouts. Used by
// addr2line.cc, and generic enough for other external filters, such as a stack
// trace colorizer.
//
// A single Subprocess instance is not safe for concurrent use from multiple
// threads. Using two different instances concurrently is fine.

#ifndef NGLOG_INTERNAL_SUBPROCESS_H
#define NGLOG_INTERNAL_SUBPROCESS_H

#include <chrono>
#include <cstddef>

#include "config.h"
#include "ng-log/export.h"
#include "ng-log/platform.h"

#ifdef HAVE_SUBPROCESS

#  if defined(NGLOG_OS_WINDOWS)
#    include <windows.h>

#    include <memory>
#    include <type_traits>
#  else
#    include <sys/types.h>

#    include "utilities.h"
#  endif  // defined(NGLOG_OS_WINDOWS)

namespace nglog {
inline namespace tools {

struct SubprocessWaitResult {
  enum Status { kExited, kTimedOut, kFailed };

  Status status;
  // Read exit_code only for kExited.
  int exit_code;

  static SubprocessWaitResult Exited(int code) {
    SubprocessWaitResult result;
    result.status = kExited;
    result.exit_code = code;
    return result;
  }

  static SubprocessWaitResult TimedOut() {
    SubprocessWaitResult result;
    result.status = kTimedOut;
    return result;
  }

  static SubprocessWaitResult Failed() {
    SubprocessWaitResult result;
    result.status = kFailed;
    result.exit_code = -1;
    return result;
  }

 private:
  SubprocessWaitResult() : status(kFailed), exit_code(-1) {}
};

#  if defined(NGLOG_OS_WINDOWS)
struct HandleDeleter {
  void operator()(HANDLE handle) const noexcept {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
      CloseHandle(handle);
    }
  }
};

// HANDLE is itself a pointer type, so it is used directly as the
// element type here (via remove_pointer_t) rather than as HANDLE
// itself, which would make unique_ptr store a HANDLE*.
using UniqueHandle =
    std::unique_ptr<std::remove_pointer_t<HANDLE>, HandleDeleter>;
#  endif  // defined(NGLOG_OS_WINDOWS)

enum class SubprocessMode { kNormal, kSignalSafe };

namespace {

template <SubprocessMode mode>
constexpr bool kSubprocessModeIsSpecialized = false;

template <>
constexpr bool kSubprocessModeIsSpecialized<SubprocessMode::kNormal> = true;

template <>
constexpr bool kSubprocessModeIsSpecialized<SubprocessMode::kSignalSafe> =
#  if !defined(NGLOG_OS_WINDOWS) && defined(HAVE__FORK) && defined(HAVE_EXECV)
    true
#  else
    false
#  endif
    ;

}  // namespace

// Spawns an external program and lets the caller exchange a bounded
// amount of data with it.
template <SubprocessMode mode = SubprocessMode::kNormal>
class NGLOG_NO_EXPORT Subprocess final {
 public:
  static_assert(kSubprocessModeIsSpecialized<mode>,
                "the selected subprocess mode is not available");

  Subprocess() noexcept = default;
  ~Subprocess();

  Subprocess(const Subprocess&) = delete;
  Subprocess& operator=(const Subprocess&) = delete;

  Subprocess(Subprocess&& other) noexcept;
  Subprocess& operator=(Subprocess&& other) noexcept;

  // The normal mode searches PATH for argv[0] and spawns it with the given
  // argv/envp (null-terminated, as for execve()/CreateProcessW()). The
  // signal-safe mode requires argv[0] to be an explicit path, ignores envp,
  // and inherits the current environment through execv(). Both modes
  // redirect stdin/stdout through pipes owned by this Subprocess and discard
  // stderr. Returns false if the process could not be spawned at all. Never
  // blocks waiting for the child.
#  if defined(NGLOG_OS_WINDOWS)
  bool Spawn(char* const argv[], char* const envp[]);
#  else
  bool Spawn(char* const argv[], char* const envp[]) noexcept;
#  endif

  // True once Spawn() has succeeded and Wait() has not yet been called.
  explicit operator bool() const noexcept;

  // Writes up to |size| bytes of |data| to the process' stdin without
  // blocking beyond |timeout|. Returns the number of bytes actually
  // written, which may be less than |size| even on success, as for a raw
  // write() call.
  //
  // Writing more than a pipe buffer's worth of input without also
  // reading stdout in between can deadlock against a child that fills
  // its own stdout pipe first. Interleave WriteStdin() with ReadStdout()
  // for large input.
  std::size_t WriteStdin(const char* data, std::size_t size,
                         std::chrono::milliseconds timeout) noexcept;

  // Closes the process' stdin, signaling EOF to it. Safe to call more
  // than once, or skip if the process needs no input.
  void CloseStdin() noexcept;

  // Reads up to |out_size| bytes of the process' stdout into |out|,
  // without blocking beyond |timeout|. Returns the number of bytes read,
  // or 0 on timeout, EOF, or error.
  std::size_t ReadStdout(char* out, std::size_t out_size,
                         std::chrono::milliseconds timeout) noexcept;

  // Waits for the process to exit, forcibly terminating it if |timeout|
  // elapses first. Always reaps the process exactly once as long as
  // Spawn() succeeded. Call at most once per Subprocess.
  SubprocessWaitResult Wait(std::chrono::milliseconds timeout) noexcept;

 private:
  void Reset() noexcept;

#  if defined(NGLOG_OS_WINDOWS)
  UniqueHandle process_;
  UniqueHandle stdin_write_;
  UniqueHandle stdout_read_;
#  else
  pid_t pid_ = -1;
  FileDescriptor stdin_write_;
  FileDescriptor stdout_read_;
#  endif  // defined(NGLOG_OS_WINDOWS)
};

}  // namespace tools
}  // namespace nglog

#endif  // HAVE_SUBPROCESS

#endif  // NGLOG_INTERNAL_SUBPROCESS_H
