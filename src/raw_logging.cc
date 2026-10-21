// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Maxim Lifantsev
//
// logging_unittest.cc covers the functionality herein

#include <cinttypes>
#include <cstdarg>  // IWYU pragma: keep
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <type_traits>

#include "config.h"

#ifdef HAVE_UNISTD_H
#  include <unistd.h>  // for syscall() and write()
#endif
#if defined(HAVE_SYS_SYSCALL_H)
#  include <sys/syscall.h>  // for SYS_write
#elif defined(HAVE_SYSCALL_H)
#  include <syscall.h>  // for SYS_write
#endif
#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "ng-log/platform.h"
#include "ng-log/raw_logging.h"
#include "stacktrace.h"
#include "utilities.h"

#ifdef NGLOG_OS_WINDOWS
#  include "internal/utf8.h"
#endif

namespace nglog {

namespace internal {

std::intmax_t SafeWrite(int fd, const void* data, std::size_t size) noexcept {
#if (defined(HAVE_SYSCALL_H) || defined(HAVE_SYS_SYSCALL_H)) && \
    !defined(NGLOG_OS_MACOSX) && !defined(NGLOG_OS_OPENBSD) &&  \
    !defined(NGLOG_OS_EMSCRIPTEN)
  return syscall(SYS_write, fd, data, size);
#else
  return write(fd, data, size);
#endif
}

}  // namespace internal

// CAVEAT: std::vsnprintf called from *DoRawLog below has some (exotic) code
// paths that invoke malloc() and getenv() that might acquire some locks. If
// this becomes a problem we should reimplement a subset of std::vsnprintf that
// does not need locks and malloc.

// Helper for RawLog below.
// *DoRawLog writes to *buf of *size and move them past the written portion.
// It returns true iff there was no overflow or error.
NGLOG_ATTRIBUTE_FORMAT(printf, 3, 4)
static bool DoRawLog(char** buf, size_t* size, const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  int n = std::vsnprintf(*buf, *size, format, ap);
  va_end(ap);
  // The terminating null character must fit as well.
  if (n < 0 || static_cast<size_t>(n) >= *size) return false;
  *size -= static_cast<size_t>(n);
  *buf += n;
  return true;
}

// Helper for RawLog below.
inline static bool VADoRawLog(char** buf, size_t* size, const char* format,
                              va_list ap) {
#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
  int n = std::vsnprintf(*buf, *size, format, ap);
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif
  // The terminating null character must fit as well.
  if (n < 0 || static_cast<size_t>(n) >= *size) return false;
  *size -= static_cast<size_t>(n);
  *buf += n;
  return true;
}

static const int kLogBufSize = 3000;
static std::once_flag crashed;
static internal::CrashReason crash_reason;
static char crash_buf[kLogBufSize + 1] = {0};  // Will end in '\0'

namespace {

// Value that std::thread::id holds and prints. POSIX threads hold the native
// handle, while the Microsoft standard library holds the numeric identifier.
#if defined(_MSVC_STL_VERSION)
using ThreadIdValue = unsigned int;
#else
using ThreadIdValue = std::thread::native_handle_type;
#endif

static_assert(sizeof(ThreadIdValue) == sizeof(std::thread::id),
              "std::thread::id must hold the native thread identifier to be "
              "written without a stream. Adapt ThreadIdValue to the standard "
              "library in use.");
static_assert(std::is_trivially_copyable<std::thread::id>::value,
              "std::thread::id must be trivially copyable to be written "
              "without a stream.");

// Streams write pointers in hexadecimal with a base prefix.
template <class T>
int FormatThreadIdValue(char* buffer, std::size_t size, T* value) {
  return std::snprintf(buffer, size, "0x%" PRIxPTR,
                       reinterpret_cast<std::uintptr_t>(value));
}

template <class T>
int FormatThreadIdValue(char* buffer, std::size_t size, T value) {
  static_assert(std::is_integral<T>::value,
                "The thread identifier must be a pointer or an integer.");
  if (std::is_signed<T>::value) {
    return std::snprintf(buffer, size, "%lld", static_cast<long long>(value));
  }
  return std::snprintf(buffer, size, "%llu",
                       static_cast<unsigned long long>(value));
}

// Writes the identifier of the calling thread like streaming
// std::this_thread::get_id() does in the classic locale. A stream cannot be
// used, since constructing its locale acquires a lock unless the global locale
// is the classic one, which is not async-signal-safe.
void FormatThreadId(char* buffer, std::size_t size) {
  const std::thread::id id = std::this_thread::get_id();
  ThreadIdValue value;
  std::memcpy(&value, &id, sizeof(value));
  if (FormatThreadIdValue(buffer, size, value) < 0) {
    buffer[0] = '\0';
  }
}

// Large enough for a prefixed 64-bit hexadecimal or a signed decimal value.
constexpr std::size_t kThreadIdBufferSize = 24;

}  // namespace

NGLOG_ATTRIBUTE_FORMAT(printf, 4, 5)
void RawLog(LogSeverity severity, const char* file, int line,
            const char* format, ...) {
  if (!(nglog::GetFlag(FLAGS_logtostdout) ||
        nglog::GetFlag(FLAGS_logtostderr) ||
        severity >= nglog::GetFlag(FLAGS_stderrthreshold) ||
        nglog::GetFlag(FLAGS_alsologtostderr) || !IsLoggingInitialized())) {
    return;  // this stderr log message is suppressed
  }

  char thread_id[kThreadIdBufferSize];
  FormatThreadId(thread_id, sizeof(thread_id));

  // can't call localtime_r here: it can allocate
  char buffer[kLogBufSize];
  char* buf = buffer;
  size_t size = sizeof(buffer);

  // NOTE: this format should match the specification in base/logging.h
  DoRawLog(&buf, &size, "%c00000000 00:00:00.000000 %5s %s:%d] RAW: ",
           GetLogSeverityName(severity)[0], thread_id,
           const_basename(const_cast<char*>(file)), line);

  // Record the position and size of the buffer after the prefix
  const char* msg_start = buf;
  const size_t msg_size = size;

  // Reserves room for the newline that ends the message.
  constexpr size_t kNewlineSize = 1;
  size_t message_size = size - kNewlineSize;
  va_list ap;
  va_start(ap, format);
  const bool no_chop = VADoRawLog(&buf, &message_size, format, ap);
  va_end(ap);
  size = message_size + kNewlineSize;
  if (no_chop) {
    DoRawLog(&buf, &size, "\n");
  } else {
    DoRawLog(&buf, &size, "RAW_LOG ERROR: The Message was too long!\n");
  }
  // We make a raw syscall to write directly to the stderr file descriptor,
  // avoiding FILE buffering (to avoid invoking malloc()), and bypassing
  // libc (to side-step any libc interception).
  // We write just once to avoid races with other invocations of RawLog.
#ifdef NGLOG_OS_WINDOWS
  internal::WriteUtf8ToFileDescriptor(fileno(stderr), buffer,
                                      std::strlen(buffer));
#else
  internal::SafeWrite(fileno(stderr), buffer, strlen(buffer));
#endif
  if (severity == NGLOG_FATAL) {
    std::call_once(crashed, [file, line, msg_start, msg_size] {
      crash_reason.filename = file;
      crash_reason.line_number = line;
      memcpy(crash_buf, msg_start, msg_size);  // Don't include prefix
      crash_reason.message = crash_buf;
#ifdef HAVE_STACKTRACE
      crash_reason.depth =
          GetStackTrace(crash_reason.stack, ARRAYSIZE(crash_reason.stack), 1);
#else
      crash_reason.depth = 0;
#endif
      SetCrashReason(&crash_reason);
    });
    LogMessage::Fail();  // abort()
  }
}

}  // namespace nglog
