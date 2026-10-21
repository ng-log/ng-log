// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Shinichiro Hamaji

#define _GNU_SOURCE 1

#include "utilities.h"

#include <sys/types.h>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <ostream>
#include <string>
#include <vector>

#include "config.h"
#include "initializer.h"
#include "internal/source_location.h"
#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "ng-log/platform.h"
#include "stacktrace.h"
#include "symbolize.h"

#if defined(HAVE_ADDR2LINE)
#  include "addr2line.h"
#elif defined(HAVE_LIBBACKTRACE)
#  include "libbacktrace.h"
#endif

#ifdef NGLOG_OS_WINDOWS
#  include <lmcons.h>
#  include <windows.h>

#  include <array>
#  include <cwchar>

#  include "internal/utf8.h"
#endif
#ifdef NGLOG_OS_ANDROID
#  include <android/log.h>
#endif
#ifdef NGLOG_OS_EMSCRIPTEN
#  include "internal/emscripten_console.h"
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
  reinterpret_cast<std::string*>(arg)->append(data);
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

// Ends a stack dump cut off at kMaxStackDumpFrames, through the sink that
// DumpPCAndSymbol() sent the frames to.
static void EndCutOffStackTrace(DebugWriter* writerfn, void* arg) {
#  if defined(HAVE_SYMBOLIZE)
  if (writerfn == &DebugWriteToStderr &&
      nglog::GetFlag(FLAGS_symbolize_stacktrace)) {
    DumpStackCutOffMarker();
    return;
  }
#  endif
  writerfn(kStackDumpCutOffMarker, arg);
}

// Dump current stack trace as directed by writerfn
static void DumpStackTrace(int skip_count, DebugWriter* writerfn, void* arg) {
  // Print stack trace. One frame more than is printed is captured, so that a
  // trace cut off at the limit says so instead of looking complete.
  void* stack[kMaxStackDumpFrames + 1];
  int depth = GetStackTrace(stack, ARRAYSIZE(stack), skip_count + 1);
  const bool truncated = depth > kMaxStackDumpFrames;
  if (truncated) {
    depth = kMaxStackDumpFrames;
  }
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
  if (truncated) {
    EndCutOffStackTrace(writerfn, arg);
  }
}

[[noreturn]] static void DumpStackTraceAndExit() {
  DumpStackTrace(1, DebugWriteToStderr, nullptr);

  if (IsFailureSignalHandlerInstalled()) {
    // Set the default signal handler for SIGABRT, to avoid invoking our
    // own signal handler installed by InstallFailureSignalHandler().
    signal(SIGABRT, SIG_DFL);
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

static int32 g_main_thread_pid = getpid();
int32 GetMainThreadPid() { return g_main_thread_pid; }

bool PidHasChanged() {
  int32 pid = getpid();
  if (g_main_thread_pid == pid) {
    return false;
  }
  g_main_thread_pid = pid;
  return true;
}

static std::string g_my_user_name;
const std::string& MyUserName() { return g_my_user_name; }

namespace {

std::string GetSystemUserName() {
#if defined(NGLOG_OS_WINDOWS)
  constexpr std::size_t kNullTerminatorSize = 1;
  constexpr DWORD kUserNameBufferSize = UNLEN + kNullTerminatorSize;
  wchar_t user_name[kUserNameBufferSize];
  DWORD user_name_size = ARRAYSIZE(user_name);
  std::string result;
  if (GetUserNameW(user_name, &user_name_size) != 0 && user_name_size > 0 &&
      internal::WideToUtf8(user_name, user_name_size - kNullTerminatorSize,
                           &result)) {
    return result;
  }
  return std::string{};
#elif defined(HAVE_PWD_H) && defined(HAVE_UNISTD_H)
  constexpr std::size_t kInitialPasswordBufferSize = 1024;
  constexpr std::size_t kPasswordBufferGrowthFactor = 2;
  const uid_t uid = geteuid();
  std::size_t buffer_size = kInitialPasswordBufferSize;
#  if defined(_SC_GETPW_R_SIZE_MAX)
  const long configured_buffer_size = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (configured_buffer_size > 0) {
    buffer_size = static_cast<std::size_t>(configured_buffer_size);
  }
#  endif

  for (;;) {
    struct passwd pwd;
    struct passwd* result = nullptr;
    std::vector<char> buffer(buffer_size);
    const int error =
        getpwuid_r(uid, &pwd, buffer.data(), buffer.size(), &result);
    if (error == 0 && result != nullptr) {
      return result->pw_name;
    }
    if (error != ERANGE ||
        buffer_size > std::numeric_limits<std::size_t>::max() /
                          kPasswordBufferGrowthFactor) {
      break;
    }
    buffer_size *= kPasswordBufferGrowthFactor;
  }
  // Users without a passwd entry, such as in containers, still get distinct
  // log file names.
  return "uid" + std::to_string(uid);
#else
  return std::string{};
#endif
}

std::string GetEnvironmentUserName() {
#if defined(NGLOG_OS_WINDOWS)
  // The narrow environment uses the ANSI code page, but ng-log requires UTF-8
  // because the user name becomes part of log file names.
  const wchar_t* user = _wgetenv(L"USERNAME");
  std::string result;
  if (user != nullptr &&
      internal::WideToUtf8(user, std::wcslen(user), &result)) {
    return result;
  }
  return std::string{};
#else
  const char* user = std::getenv("USER");
  return user != nullptr ? user : std::string{};
#endif
}

std::string GetMyUserName() {
  std::string user_name = GetEnvironmentUserName();
  if (!user_name.empty()) {
    return user_name;
  }

  user_name = GetSystemUserName();
  if (user_name.empty()) {
    user_name = "invalid-user";
  }
  return user_name;
}

}  // namespace

static void MyUserNameInitializer() { g_my_user_name = GetMyUserName(); }
REGISTER_MODULE_INITIALIZER(utilities, MyUserNameInitializer())

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
