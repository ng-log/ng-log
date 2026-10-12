// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Shinichiro Hamaji

#define _GNU_SOURCE 1

#include "utilities.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "addr2line.h"
#include "config.h"
#include "internal/emscripten_console.h"
#include "internal/no_destructor.h"
#include "internal/source_location.h"
#include "internal/utf8.h"
#include "libbacktrace.h"
#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "stacktrace.h"
#include "symbolize.h"

#ifdef NGLOG_OS_ANDROID
#  include <android/log.h>
#endif
#ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
#endif
#if defined(HAVE_SYSCALL_H)
#  include <syscall.h>  // for syscall()
#elif defined(HAVE_SYS_SYSCALL_H)
#  include <sys/syscall.h>  // for syscall()
#endif
#ifdef HAVE_SYSLOG_H
#  include <syslog.h>
#endif
#ifdef HAVE_UNISTD_H
#  include <unistd.h>  // For geteuid.
#endif
#ifdef HAVE_PWD_H
#  include <pwd.h>
#endif
#ifdef HAVE_PTHREAD_SETNAME_NP
#  include <pthread.h>
#endif
#ifdef NGLOG_OS_WINDOWS
#  include <windows.h>
#endif

#if defined(HAVE___PROGNAME)
extern char* __progname;
#endif

namespace {

constexpr char log_filename_timestamp_pattern[] = "[0-9]{8}-[0-9]{6}\\.[0-9]+";
constexpr char log_filename_never_match_pattern[] = "a^";

#if defined(NGLOG_OS_WINDOWS)
constexpr std::size_t kTerminatorLength = 1;
constexpr std::size_t kDebuggerBufferLength =
    nglog::internal::kMaximumWideChunkLength + kTerminatorLength;

bool WriteWideToDebugger(const wchar_t* input, std::size_t input_length,
                         std::size_t* written, void*) {
  std::array<wchar_t, kDebuggerBufferLength> buffer = {};
  if (input_length >= buffer.size()) {
    return false;
  }

  std::copy_n(input, input_length, buffer.data());
  buffer[input_length] = L'\0';
  ::OutputDebugStringW(buffer.data());
  *written = input_length;
  return true;
}

// OutputDebugStringW() stops at the first null character, so embedded null
// characters are skipped rather than truncating the remaining text.
bool WriteWideToDebuggerSkippingNulls(const wchar_t* input,
                                      std::size_t input_length,
                                      std::size_t* written, void* context) {
  if (!nglog::internal::WriteWideAsChunks(input, input_length,
                                          WriteWideToDebugger, context)) {
    return false;
  }
  *written = input_length;
  return true;
}
#endif

std::string EscapeRegexCharacters(const std::string& input) {
  constexpr char regex_characters[] = "\\.^$|()[]{}*+?";
  std::string result;
  result.reserve(input.size());
  for (const char character : input) {
    if (std::strchr(regex_characters, character) != nullptr) {
      result += '\\';
    }
    result += character;
  }
  return result;
}

}  // namespace

using std::string;

namespace nglog {

static const char* g_program_invocation_short_name = nullptr;

bool IsLoggingInitialized() {
  return g_program_invocation_short_name != nullptr;
}

inline namespace tools {

constexpr int FileDescriptor::InvalidHandle;

std::string MakeLogFilename(const std::string& base_filename,
                            const std::string& time_pid_string,
                            const std::string& filename_extension,
                            bool timestamp_in_logfile_name) {
  std::string filename = base_filename;
  if (timestamp_in_logfile_name) {
    filename += time_pid_string;
  }
  filename += filename_extension;
  return filename;
}

std::regex MakeLogFilenameMatcher(const std::string& base_filename,
                                  const std::string& filename_extension,
                                  bool timestamp_in_logfile_name) {
  // An empty base would make a timestamp rule match unrelated files. Exact
  // current-process filenames remain available to the log cleaner separately.
  if (base_filename.empty() && timestamp_in_logfile_name) {
    return std::regex(log_filename_never_match_pattern);
  }

  std::string pattern = "^" + EscapeRegexCharacters(base_filename);
  if (timestamp_in_logfile_name) {
    pattern += log_filename_timestamp_pattern;
  }
  pattern += EscapeRegexCharacters(filename_extension);
  pattern += '$';
  return std::regex(pattern);
}

void AlsoErrorWrite(LogSeverity severity, const char* tag, const char* message,
                    std::size_t message_length) noexcept {
#if defined(NGLOG_OS_WINDOWS)
  (void)severity;
  (void)tag;
  // On Windows, also output to the debugger
  internal::WriteUtf8AsWide(message, message_length,
                            WriteWideToDebuggerSkippingNulls, nullptr);
#elif defined(NGLOG_OS_ANDROID)
  constexpr int android_log_levels[] = {
      ANDROID_LOG_INFO,
      ANDROID_LOG_WARN,
      ANDROID_LOG_ERROR,
      ANDROID_LOG_FATAL,
  };

  (void)message_length;
  __android_log_write(android_log_levels[severity], tag, message);
#elif defined(NGLOG_OS_EMSCRIPTEN)
  (void)tag;
  (void)message_length;
  internal::WriteEmscriptenLog(
      internal::EmscriptenLogLevelForSeverity(severity), message);
#else
  (void)severity;
  (void)tag;
  (void)message;
  (void)message_length;
#endif
}

void SetThreadName(const char* name) {
  if (name == nullptr) {
    return;
  }

#if defined(NGLOG_OS_WINDOWS) && defined(HAVE_GET_THREAD_DESCRIPTION)
  const int wide_name_size =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, nullptr, 0);
  if (wide_name_size <= 0) {
    return;
  }

  std::wstring wide_name(static_cast<std::size_t>(wide_name_size), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1,
                          &wide_name[0], wide_name_size) <= 0) {
    return;
  }
  SetThreadDescription(GetCurrentThread(), wide_name.c_str());
#elif defined(HAVE_PTHREAD_SETNAME_NP)
#  if defined(NGLOG_OS_MACOSX)
  pthread_setname_np(name);
#  else
  pthread_setname_np(pthread_self(), name);
#  endif
#else
  (void)name;
#endif
}

}  // namespace tools

}  // namespace nglog

// The following APIs are all internal.
#ifdef HAVE_STACKTRACE

#  include "ng-log/flags.h"
#  include "stacktrace.h"
#  include "symbolize.h"

namespace nglog {

using DebugWriter = void(const char*, void*);

// The %p field width for printf() functions is two characters per byte.
// For some environments, add two extra bytes for the leading "0x".
static const int kPrintfPointerFieldWidth = 2 + 2 * sizeof(void*);

static void DebugWriteToStderr(const char* data, void*) {
  // This one is signal-safe.
#  ifdef NGLOG_OS_WINDOWS
  internal::WriteUtf8ToFileDescriptor(fileno(stderr), data, std::strlen(data));
#  else
  if (write(fileno(stderr), data, std::strlen(data)) < 0) {
    // Ignore errors.
  }
#  endif
  AlsoErrorWrite(NGLOG_FATAL, tools::ProgramInvocationShortName(), data,
                 std::strlen(data));
}

static void DebugWriteToString(const char* data, void* arg) {
  reinterpret_cast<string*>(arg)->append(data);
}

#  ifdef HAVE_SYMBOLIZE
// Print a program counter and its symbol name.
static void DumpPCAndSymbol(DebugWriter* writerfn, void* arg, void* pc,
                            const char* const prefix) {
  if (writerfn == &DebugWriteToStderr) {
    DumpStackFrameInfo(prefix, pc);
    return;
  }

  char tmp[1024];
  const char* symbol = "(unknown)";
  SymbolizedFrame frame;
  // Symbolizes the previous address of pc because pc may be in the
  // next function.  The overrun happens when the function ends with
  // a call to a function annotated noreturn (e.g. CHECK).
  if (Symbolize(reinterpret_cast<char*>(pc) - 1, tmp, sizeof(tmp),
                SymbolizeOptions::kNone, &frame)) {
    symbol = tmp;
  }
  char formatted_symbol[sizeof(tmp)];
  if (frame.file_line_length > 0) {
    internal::FormatSymbolizedFrame(
        symbol, std::strlen(symbol), frame.file_line_offset,
        frame.file_line_length, internal::CachedCwd().c_str(), formatted_symbol,
        sizeof(formatted_symbol));
    symbol = formatted_symbol;
  }
  char buf[1024];
  std::snprintf(buf, sizeof(buf), "%s@ %*p  %s\n", prefix,
                kPrintfPointerFieldWidth, pc, symbol);
  writerfn(buf, arg);
}
#  endif

static void DumpPC(DebugWriter* writerfn, void* arg, void* pc,
                   const char* const prefix) {
  char buf[100];
  std::snprintf(buf, sizeof(buf), "%s@ %*p\n", prefix, kPrintfPointerFieldWidth,
                pc);
  writerfn(buf, arg);
}

// Dump current stack trace as directed by writerfn
static void DumpStackTrace(int skip_count, DebugWriter* writerfn, void* arg) {
  // Print stack trace
  void* stack[32];
  int depth = GetStackTrace(stack, ARRAYSIZE(stack), skip_count + 1);
  for (int i = 0; i < depth; i++) {
#  if defined(HAVE_SYMBOLIZE)
    if (nglog::GetFlag(FLAGS_symbolize_stacktrace)) {
      DumpPCAndSymbol(writerfn, arg, stack[i], "    ");
    } else {
      DumpPC(writerfn, arg, stack[i], "    ");
    }
#  else
    DumpPC(writerfn, arg, stack[i], "    ");
#  endif
  }
}

[[noreturn]] static void DumpStackTraceAndExit() {
  DumpStackTrace(1, DebugWriteToStderr, nullptr);

  // TODO(hamaji): Use signal instead of sigaction?
  if (IsFailureSignalHandlerInstalled()) {
    // Set the default signal handler for SIGABRT, to avoid invoking our
    // own signal handler installed by InstallFailureSignalHandler().
#  ifdef HAVE_SIGACTION
    struct sigaction sig_action;
    memset(&sig_action, 0, sizeof(sig_action));
    sigemptyset(&sig_action.sa_mask);
    sig_action.sa_handler = SIG_DFL;
    sigaction(SIGABRT, &sig_action, nullptr);
#  elif defined(NGLOG_OS_WINDOWS)
    signal(SIGABRT, SIG_DFL);
#  endif  // HAVE_SIGACTION
  }

  abort();
}

}  // namespace nglog

#endif  // HAVE_STACKTRACE

namespace nglog {

inline namespace tools {

std::string TrimTrailingCharacters(std::string input, const char* characters,
                                   std::size_t character_count) {
  const std::string::size_type pos =
      input.find_last_not_of(characters, std::string::npos, character_count);
  if (pos == std::string::npos) {
    return {};
  }
  input.erase(pos + 1);
  return input;
}

std::string TrimTrailingCRLF(std::string message) {
  return TrimTrailingCharacters(std::move(message), "\r\n");
}

const char* const_basename(const char* filepath) {
  const char* base = strrchr(filepath, '/');
#ifdef NGLOG_OS_WINDOWS  // Look for either path separator in Windows
  if (!base) base = strrchr(filepath, '\\');
#endif
  return base ? (base + 1) : filepath;
}

const char* ProgramInvocationShortName() {
  if (g_program_invocation_short_name != nullptr) {
    return g_program_invocation_short_name;
  }
#if defined(HAVE_PROGRAM_INVOCATION_SHORT_NAME)
  return program_invocation_short_name;
#elif defined(HAVE_GETPROGNAME)
  return getprogname();
#elif defined(HAVE___PROGNAME)
  return __progname;
#elif defined(HAVE___ARGV)
  return const_basename(__argv[0]);
#else
  return "UNKNOWN";
#endif
}

// Zero is never the ID of a running process, so it marks the ID of the main
// thread as not yet recorded.
static std::atomic<int32> g_main_thread_pid{0};

int32 GetMainThreadPid() {
  int32 pid = g_main_thread_pid.load(std::memory_order_relaxed);
  if (pid == 0) {
    const auto current = static_cast<int32>(getpid());
    // Keep the ID recorded concurrently by another thread, if any.
    if (g_main_thread_pid.compare_exchange_strong(pid, current,
                                                  std::memory_order_relaxed)) {
      pid = current;
    }
  }
  return pid;
}

bool PidHasChanged() {
  const auto pid = static_cast<int32>(getpid());
  const int32 previous =
      g_main_thread_pid.exchange(pid, std::memory_order_relaxed);
  return previous != 0 && previous != pid;
}

static string QueryUserName() {
  string name;
  // TODO(hamaji): Probably this is not portable.
#if defined(NGLOG_OS_WINDOWS)
  const char* user = getenv("USERNAME");
#else
  const char* user = getenv("USER");
#endif
  if (user != nullptr) {
    name = user;
  } else {
#if defined(HAVE_PWD_H) && defined(HAVE_UNISTD_H)
    struct passwd pwd;
    struct passwd* result = nullptr;
    char buffer[1024] = {'\0'};
    uid_t uid = geteuid();
    int pwuid_res = getpwuid_r(uid, &pwd, buffer, sizeof(buffer), &result);
    if (pwuid_res == 0 && result) {
      name = pwd.pw_name;
    } else {
      std::snprintf(buffer, sizeof(buffer), "uid%d", uid);
      name = buffer;
    }
#endif
    if (name.empty()) {
      name = "invalid-user";
    }
  }
  return name;
}

const string& MyUserName() {
  static const internal::NoDestructor<string> name{QueryUserName()};
  return *name;
}

// We use an atomic operation to prevent problems with calling CrashReason
// from inside the Mutex implementation (potentially through RAW_CHECK).
static std::atomic<const internal::CrashReason*> g_reason{nullptr};

void SetCrashReason(const internal::CrashReason* r) {
  const internal::CrashReason* expected = nullptr;
  g_reason.compare_exchange_strong(expected, r);
}

void InitializeLoggingUtilities(const char* argv0) {
  CHECK(!IsLoggingInitialized()) << "You called InitializeLogging() twice!";
  g_program_invocation_short_name = const_basename(argv0);

#ifdef HAVE_STACKTRACE
  InstallFailureFunction(&DumpStackTraceAndExit);
#endif
#ifdef HAVE_LIBBACKTRACE
  if (nglog::GetFlag(FLAGS_symbolize_line_info)) {
    InstallLibbacktraceSymbolizeCallback();
  }
#elif defined(HAVE_ADDR2LINE)
  if (nglog::GetFlag(FLAGS_symbolize_line_info)) {
    InstallAddr2LineSymbolizeCallback();
  }
#endif
}

void ShutdownLoggingUtilities() {
  CHECK(IsLoggingInitialized())
      << "You called ShutdownLogging() without calling "
         "InitializeLogging() first!";
  g_program_invocation_short_name = nullptr;
#ifdef HAVE_SYSLOG_H
  closelog();
#endif
}

}  // namespace tools

#ifdef HAVE_STACKTRACE
std::string GetStackTrace() {
  std::string stacktrace;
  DumpStackTrace(1, DebugWriteToString, &stacktrace);
  return stacktrace;
}
#endif

}  // namespace nglog
