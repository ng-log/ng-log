// Copyright (c) 2024, Google Inc.
// Copyright (c) 2026, The ng-log contributors
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Author: Satoru Takabayashi
//
// Implementation of InstallFailureSignalHandler().

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <sstream>
#include <string>
#include <thread>

#include "config.h"
#include "internal/source_location.h"
#include "internal/styled_output.h"
#include "internal/terminal_capabilities.h"
#include "internal/theme.h"
#include "internal/utf8.h"
#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "ng-log/platform.h"
#include "stacktrace.h"
#include "symbolize.h"
#include "utilities.h"

#ifdef HAVE_UCONTEXT_H
#  include <ucontext.h>
#endif
#ifdef HAVE_SYS_UCONTEXT_H
#  include <sys/ucontext.h>
#endif
#ifdef HAVE_PTHREAD_GETNAME_NP
#  include <pthread.h>
#endif
#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif
#if defined(HAVE_SYS_SYSCALL_H) && defined(HAVE_SYS_TYPES_H)
#  include <sys/syscall.h>
#  include <sys/types.h>
#endif
#ifdef NGLOG_OS_WINDOWS
#  include <windows.h>
#else  // !defined(NGLOG_OS_WINDOWS)
#  ifdef HAVE_SCHED_YIELD
#    include <sched.h>
#  endif  // defined(HAVE_SCHED_YIELD)
#endif    // defined(NGLOG_OS_WINDOWS)

namespace nglog {

using namespace internal;

namespace {

// We'll install the failure signal handler for these signals.  We could
// use strsignal() to get signal names, but we don't use it to avoid
// introducing yet another #ifdef complication.
//
// The list should be synced with the comment in signalhandler.h.
const struct {
  int number;
  const char* name;
} kFailureSignals[] = {
    {SIGSEGV, "SIGSEGV"}, {SIGILL, "SIGILL"},
    {SIGFPE, "SIGFPE"},   {SIGABRT, "SIGABRT"},
#if !defined(NGLOG_OS_WINDOWS)
    {SIGBUS, "SIGBUS"},
#endif
    {SIGTERM, "SIGTERM"},
};

#if defined(NGLOG_OS_WINDOWS)
static bool kFailureSignalHandlerInstalled = false;
#endif  // defined(NGLOG_OS_WINDOWS)

#if !defined(NGLOG_OS_WINDOWS)
// Returns the program counter from signal context, nullptr if unknown.
void* GetPC(void* ucontext_in_void) {
#  if (defined(HAVE_UCONTEXT_H) || defined(HAVE_SYS_UCONTEXT_H)) && \
      defined(PC_FROM_UCONTEXT)
  if (ucontext_in_void != nullptr) {
    ucontext_t* context = reinterpret_cast<ucontext_t*>(ucontext_in_void);
    return (void*)context->PC_FROM_UCONTEXT;
  }
#  else
  (void)ucontext_in_void;
#  endif
  return nullptr;
}
#endif

// Indexed by digit value rather than computed via character arithmetic
// ('a' + digit - 10 or similar) in MinimalFormatter::AppendUint64() below:
// while '0'-'9' are guaranteed contiguous by the standard, 'a'-'f' are not
// guaranteed to be (e.g. EBCDIC), so that arithmetic would not necessarily
// produce the intended letters.
constexpr char kHexDigits[] = "0123456789abcdef";

// The class is used for formatting error messages.  We don't use printf()
// as it's not async signal safe.
class MinimalFormatter {
 public:
  MinimalFormatter(char* buffer, size_t size)
      : buffer_(buffer), cursor_(buffer), end_(buffer + size) {}

  // Returns the number of bytes written in the buffer.
  std::size_t num_bytes_written() const {
    return static_cast<std::size_t>(cursor_ - buffer_);
  }

  // Rewinds the cursor to the start of the buffer, discarding its content,
  // so the same buffer can be reused for the next plain-text run between
  // colorized fields.
  void Reset() { cursor_ = buffer_; }

  // Appends string from "str" and updates the internal cursor.
  void AppendString(const char* str) {
    ptrdiff_t i = 0;
    while (str[i] != '\0' && cursor_ + i < end_) {
      cursor_[i] = str[i];
      ++i;
    }
    cursor_ += i;
  }

  // Appends exactly "len" bytes from "str", which need not be
  // NUL-terminated (or may contain embedded NULs), and updates the
  // internal cursor.
  void AppendString(const char* str, size_t len) {
    size_t i = 0;
    while (i < len && cursor_ + i < end_) {
      cursor_[i] = str[i];
      ++i;
    }
    cursor_ += i;
  }

  // Formats "number" in "radix" and updates the internal cursor.
  // Lowercase letters are used for 'a' - 'z'.
  void AppendUint64(uint64 number, unsigned radix) {
    unsigned i = 0;
    while (cursor_ + i < end_) {
      const uint64 tmp = number % radix;
      number /= radix;
      cursor_[i] = kHexDigits[tmp];
      ++i;
      if (number == 0) {
        break;
      }
    }
    // Reverse the bytes written.
    std::reverse(cursor_, cursor_ + i);
    cursor_ += i;
  }

  // Formats "number" as hexadecimal number, and updates the internal
  // cursor.  Padding will be added in front if needed.
  void AppendHexWithPadding(uint64 number, int width) {
    char* const start = cursor_;
    AppendString("0x");
    AppendUint64(number, 16);
    const int64 content_len = cursor_ - start;
    if (content_len < width) {
      // The padded field, [start, start + target_len), must include both
      // the shifted-right content and the padding spaces in front of it.
      // target_len is clamped to the buffer's remaining capacity (from
      // "start", not "cursor_") so that a "width" wider than the buffer
      // can never make the std::copy()/std::fill() below write past it.
      const int64 target_len =
          std::min(static_cast<int64>(width), static_cast<int64>(end_ - start));
      const int64 delta = target_len - content_len;  // Padding spaces.
      if (delta > 0) {
        std::copy(start, cursor_, start + delta);
        std::fill(start, start + delta, ' ');
        cursor_ = start + target_len;
      }
    }
  }

 private:
  char* buffer_;
  char* cursor_;
  const char* const end_;
};

// Writes the given data with the size to the standard error.
void WriteToStderr(const char* data, size_t size) {
#ifdef NGLOG_OS_WINDOWS
  internal::WriteSignalSafeToFileDescriptor(fileno(stderr), data, size);
#else
  if (write(fileno(stderr), data, size) < 0) {
    // Ignore errors.
  }
#endif
}

// The writer function can be changed by InstallFailureWriter().
void (*g_failure_writer)(const char* data, size_t size) = WriteToStderr;

#if defined(HAVE_STACKTRACE) && defined(HAVE_SIGACTION)
constexpr std::size_t kThreadNameBufferSize = 64;
constexpr std::size_t kTerminatorLength = 1;

// Called from the failure signal handler, so this function must remain
// async-signal-safe.
bool GetThreadName(char* buffer, std::size_t buffer_size) {
  if (buffer_size == 0) {
    return false;
  }
  buffer[0] = '\0';

#  if defined(NGLOG_OS_WINDOWS) && defined(HAVE_GET_THREAD_DESCRIPTION)
  PWSTR wide_name = nullptr;
  if (FAILED(GetThreadDescription(GetCurrentThread(), &wide_name)) ||
      wide_name == nullptr) {
    return false;
  }
  std::size_t utf8_name_length = 0;
  const bool converted = internal::WideToUtf8Buffer(
      wide_name, std::wcslen(wide_name), buffer,
      buffer_size - kTerminatorLength, &utf8_name_length);
  LocalFree(wide_name);
  if (!converted || utf8_name_length == 0) {
    return false;
  }
  buffer[utf8_name_length] = '\0';
  return true;
#  elif defined(HAVE_PTHREAD_GETNAME_NP)
  return pthread_getname_np(pthread_self(), buffer, buffer_size) == 0 &&
         buffer[0] != '\0';
#  else
  return false;
#  endif
}
#endif  // defined(HAVE_STACKTRACE) && defined(HAVE_SIGACTION)

// How (if at all) stack frames dumped to stderr may be colorized (and,
// separately, whether they may be hyperlinked). Computed once by
// InstallFailureSignalHandler(), which is not run from a signal handler,
// and simply read from HandleSignal(), which is: neither getenv() nor
// isatty() (used to compute these) is async-signal-safe, so they cannot be
// (re-)evaluated once a signal has actually fired. Coloring is skipped
// entirely once a caller installs a writer other than the default
// WriteToStderr(), since these decisions were made for the literal
// "stderr" stream and may no longer apply to wherever the custom writer
// actually sends its output. See the "mode" computation at each call site
// below.
ColorMode g_stderr_color_mode = ColorMode::kNone;
bool g_stderr_hyperlinks = false;

// The local hostname, used as the authority component of "file://" URIs in
// stack trace hyperlinks (see BuildFileUri() in color.h): terminal
// implementations of OSC 8 file links (e.g. iTerm2, VS Code) compare it
// against their own hostname before offering to open the link, which
// matters for a log that might be viewed on a different machine than the
// one that produced it. Resolved once by InstallFailureSignalHandler(),
// since gethostname() is not async-signal-safe.
char g_hostname[256] = "";

void ComputeHostname() {
#ifdef NGLOG_OS_WINDOWS
  std::string hostname;
  if (nglog::internal::GetComputerNameUtf8(&hostname)) {
    MinimalFormatter formatter(g_hostname, sizeof(g_hostname));
    formatter.AppendString(hostname.c_str());
  }
#elif defined(HAVE_UNISTD_H)
  if (gethostname(g_hostname, sizeof(g_hostname)) != 0) {
    g_hostname[0] = '\0';
  }
  g_hostname[sizeof(g_hostname) - 1] = '\0';  // POSIX doesn't guarantee this.
#endif  // NGLOG_OS_WINDOWS
}

// The current working directory, used to display a stack frame's file
// path relative to it (see FormatDisplayPath() in color.h) instead of by
// its full absolute path, when it is a descendant of it, as a project's
// own source files usually are, unlike system/library headers. Resolved
// once by InstallFailureSignalHandler(), since getcwd() is not
// async-signal-safe.
char g_cwd[512] = "";

void ComputeCwd() {
#ifdef NGLOG_OS_WINDOWS
  wchar_t wide_cwd[sizeof(g_cwd)];
  const DWORD wide_cwd_size = sizeof(wide_cwd) / sizeof(wide_cwd[0]);
  const DWORD len = GetCurrentDirectoryW(wide_cwd_size, wide_cwd);
  std::string cwd;
  if (len == 0 || len >= wide_cwd_size ||
      !internal::WideToUtf8(wide_cwd, len, &cwd)) {
    g_cwd[0] = '\0';
  } else {
    const std::size_t copy_length = std::min(cwd.size(), sizeof(g_cwd) - 1);
    std::memcpy(g_cwd, cwd.data(), copy_length);
    g_cwd[copy_length] = '\0';
  }
#elif defined(HAVE_UNISTD_H)
  if (getcwd(g_cwd, sizeof(g_cwd)) == nullptr) {
    g_cwd[0] = '\0';
  }
#endif  // NGLOG_OS_WINDOWS
}

// Dumps time information.  We don't dump human-readable time information
// as localtime() is not guaranteed to be async signal safe.
void DumpTimeInfo() {
  time_t time_in_sec = time(nullptr);
  const ColorMode mode = g_failure_writer == &WriteToStderr
                             ? g_stderr_color_mode
                             : ColorMode::kNone;
  const Theme& theme = DefaultTheme();

  char plain_buf[64];  // Big enough for the plain-text runs below.
  MinimalFormatter plain(plain_buf, sizeof(plain_buf));
  plain.AppendString("*** Aborted at ");
  g_failure_writer(plain_buf, plain.num_bytes_written());

  {
    // Same role as the "date -d @<unix time>" suggestion below, since
    // this is the same value, just in a different format.
    char time_buf[24];  // Big enough for a 64-bit decimal unix time.
    MinimalFormatter time_formatter(time_buf, sizeof(time_buf));
    time_formatter.AppendUint64(static_cast<uint64>(time_in_sec), 10);
    WriteStyledField(theme.Get(Role::kShellCommand), mode, g_failure_writer,
                     time_buf, time_formatter.num_bytes_written());
  }

  plain.Reset();
  plain.AppendString(" (unix time) try \"");
  g_failure_writer(plain_buf, plain.num_bytes_written());

  {
    // The suggested command itself, highlighted like the file:line
    // references in a stack trace: it is also something a reader might
    // want to select and run.
    char command_buf[64];
    MinimalFormatter command(command_buf, sizeof(command_buf));
    command.AppendString("date -d @");
    command.AppendUint64(static_cast<uint64>(time_in_sec), 10);
    WriteStyledField(theme.Get(Role::kShellCommand), mode, g_failure_writer,
                     command_buf, command.num_bytes_written());
  }

  plain.Reset();
  plain.AppendString("\" if you are using GNU date ***\n");
  g_failure_writer(plain_buf, plain.num_bytes_written());
}

// TODO(hamaji): Use signal instead of sigaction?
#if defined(HAVE_STACKTRACE) && defined(HAVE_SIGACTION)

// Dumps information about the signal to STDERR. The crash address and the
// PID/TID/LWP identifiers are colorized separately from the surrounding
// text, using the same roles DumpStackFrameInfo() below uses for the
// analogous fields (Role::kStackAddress for the address, so an address
// reads the same way wherever it appears. Role::kMetaIdentifier for the
// process/thread identifiers), for a visually consistent crash report.
void DumpSignalInfo(int signal_number, siginfo_t* siginfo) {
  // Get the signal name.
  const char* signal_name = nullptr;
  for (auto kFailureSignal : kFailureSignals) {
    if (signal_number == kFailureSignal.number) {
      signal_name = kFailureSignal.name;
    }
  }

  // This function only runs on POSIX (see the enclosing "#if
  // defined(HAVE_SIGACTION)" a few lines up), so g_stderr_color_mode is
  // never ColorMode::kLegacyConsole (Windows-only) here.
  const ColorMode mode = g_failure_writer == &WriteToStderr
                             ? g_stderr_color_mode
                             : ColorMode::kNone;
  const Theme& theme = DefaultTheme();

  char plain_buf[128];  // Big enough for the plain-text runs below.
  MinimalFormatter plain(plain_buf, sizeof(plain_buf));

  plain.AppendString("*** ");
  g_failure_writer(plain_buf, plain.num_bytes_written());

  if (signal_name != nullptr) {
    WriteStyledField(theme.Get(Role::kSignalName), mode, g_failure_writer,
                     signal_name, std::strlen(signal_name));
  } else {
    // Use the signal number if the name is unknown.  The signal name
    // should be known, but just in case.
    char buf[24];  // Big enough for "Signal " + a 64-bit decimal number.
    MinimalFormatter formatter(buf, sizeof(buf));
    formatter.AppendString("Signal ");
    formatter.AppendUint64(static_cast<uint64>(signal_number), 10);
    WriteStyledField(theme.Get(Role::kSignalName), mode, g_failure_writer, buf,
                     formatter.num_bytes_written());
  }

  plain.Reset();
  plain.AppendString(" (@");
  g_failure_writer(plain_buf, plain.num_bytes_written());

  {
    // "0x" is part of the colored field, not the surrounding plain text,
    // to match how DumpStackFrameInfo() colors the "0x"-prefixed address
    // of each frame as a single unit.
    char buf[24];  // Big enough for a padded, prefixed 64-bit address.
    MinimalFormatter formatter(buf, sizeof(buf));
    formatter.AppendString("0x");
    formatter.AppendUint64(reinterpret_cast<uintptr_t>(siginfo->si_addr), 16);
    WriteStyledField(theme.Get(Role::kStackAddress), mode, g_failure_writer,
                     buf, formatter.num_bytes_written());
  }

  plain.Reset();
  plain.AppendString(") received by PID ");
  g_failure_writer(plain_buf, plain.num_bytes_written());

  {
    char buf[24];  // Big enough for a 64-bit decimal PID.
    MinimalFormatter formatter(buf, sizeof(buf));
    formatter.AppendUint64(static_cast<uint64>(getpid()), 10);
    WriteStyledField(theme.Get(Role::kMetaIdentifier), mode, g_failure_writer,
                     buf, formatter.num_bytes_written());
  }

  plain.Reset();
  plain.AppendString(" (TID ");
  g_failure_writer(plain_buf, plain.num_bytes_written());

  {
    std::ostringstream oss;
    oss << std::showbase << std::hex << std::this_thread::get_id();
    const std::string tid_str = oss.str();
    WriteStyledField(theme.Get(Role::kMetaIdentifier), mode, g_failure_writer,
                     tid_str.c_str(), tid_str.size());
  }

#  if defined(NGLOG_OS_LINUX) && defined(HAVE_SYS_SYSCALL_H) && \
      defined(HAVE_SYS_TYPES_H)
  {
    long tid = syscall(SYS_gettid);
    plain.Reset();
    plain.AppendString(" LWP ");
    g_failure_writer(plain_buf, plain.num_bytes_written());

    char buf[24];  // Big enough for a 64-bit decimal LWP id.
    MinimalFormatter formatter(buf, sizeof(buf));
    formatter.AppendUint64(static_cast<uint64>(tid), 10);
    WriteStyledField(theme.Get(Role::kMetaIdentifier), mode, g_failure_writer,
                     buf, formatter.num_bytes_written());
  }
#  endif  // GLOG_OS_LINUX && HAVE_SYS_SYSCALL_H && HAVE_SYS_TYPES_H

  char thread_name[kThreadNameBufferSize];
  if (GetThreadName(thread_name, sizeof(thread_name))) {
    plain.Reset();
    plain.AppendString(" thread \"");
    g_failure_writer(plain_buf, plain.num_bytes_written());
    WriteStyledField(theme.Get(Role::kMetaThreadName), mode, g_failure_writer,
                     thread_name, std::strlen(thread_name));
    plain.Reset();
    plain.AppendString("\"");
    g_failure_writer(plain_buf, plain.num_bytes_written());
  }

  plain.Reset();
  plain.AppendString(") ");
  // Only linux has the PID of the signal sender in si_pid.
#  ifdef NGLOG_OS_LINUX
  plain.AppendString("from PID ");
#  endif  // NGLOG_OS_LINUX
  g_failure_writer(plain_buf, plain.num_bytes_written());

#  ifdef NGLOG_OS_LINUX
  {
    char buf[24];  // Big enough for a 64-bit decimal PID.
    MinimalFormatter formatter(buf, sizeof(buf));
    formatter.AppendUint64(static_cast<uint64>(siginfo->si_pid), 10);
    WriteStyledField(theme.Get(Role::kMetaIdentifier), mode, g_failure_writer,
                     buf, formatter.num_bytes_written());
  }
  plain.Reset();
  plain.AppendString("; ");
  g_failure_writer(plain_buf, plain.num_bytes_written());
#  endif  // NGLOG_OS_LINUX

  plain.Reset();
  plain.AppendString("stack trace: ***\n");
  g_failure_writer(plain_buf, plain.num_bytes_written());
}

#endif  // HAVE_SIGACTION

// Dumps information about the stack frame to STDERR.
void DumpStackFrameInfo(const char* prefix, void* pc) {
  // Get the symbol name.
  const char* symbol = "(unknown)";
  bool symbol_resolved = false;
#if defined(HAVE_SYMBOLIZE)
  SymbolizedFrame frame;
  char symbolized[1024];  // Big enough for a sane symbol.
  // Symbolizes the previous address of pc because pc may be in the
  // next function.
  if (Symbolize(reinterpret_cast<char*>(pc) - 1, symbolized, sizeof(symbolized),
                SymbolizeOptions::kNone, &frame)) {
    symbol = symbolized;
    symbol_resolved = true;
  }
#else
#  pragma message( \
      "Symbolize functionality is not available for target platform: stack dump will contain empty frames.")
#endif  // defined(HAVE_SYMBOLIZE)

  // Only colorize/hyperlink if the writer is still the default one that
  // actually targets stderr. g_stderr_color_mode/g_stderr_hyperlinks were
  // computed for that specific stream.
  const ColorMode mode = g_failure_writer == &WriteToStderr
                             ? g_stderr_color_mode
                             : ColorMode::kNone;
#if defined(HAVE_SYMBOLIZE)
  const bool hyperlink = mode == ColorMode::kAnsi &&
                         FLAGS_symbolize_hyperlinks && g_stderr_hyperlinks &&
                         frame.file_line_length > 0;
  char uri[1024];
  const char* uri_pointer = nullptr;
  if (hyperlink &&
      BuildFileUri(symbol + frame.file_line_offset, frame.file_line_length,
                   FLAGS_symbolize_file_base_path.c_str(), g_hostname, uri,
                   sizeof(uri))) {
    uri_pointer = uri;
  }
#endif  // defined(HAVE_SYMBOLIZE)

  const Theme& theme = DefaultTheme();

  char plain_buf[64];  // Big enough for the plain-text runs below.
  MinimalFormatter plain(plain_buf, sizeof(plain_buf));
  plain.AppendString(prefix);
  plain.AppendString("@ ");
  g_failure_writer(plain_buf, plain.num_bytes_written());

  {
    char addr_buf[32];  // Big enough for a padded, prefixed 64-bit address.
    MinimalFormatter addr_formatter(addr_buf, sizeof(addr_buf));
    const int width = 2 * sizeof(void*) + 2;  // + 2  for "0x".
    addr_formatter.AppendHexWithPadding(reinterpret_cast<uintptr_t>(pc), width);
    WriteStyledField(theme.Get(Role::kStackAddress), mode, g_failure_writer,
                     addr_buf, addr_formatter.num_bytes_written());
  }

  g_failure_writer(" ", 1);

#if defined(HAVE_SYMBOLIZE)
  if (frame.file_line_length > 0) {
    std::size_t function_len = 0;
    if (frame.file_line_offset > 0) {
      // DbgHelp is the only backend that reports the function before the
      // file:line span. Its " (" separator is not part of the function.
      function_len = frame.file_line_offset;
      if (function_len >= 2 && symbol[function_len - 2] == ' ' &&
          symbol[function_len - 1] == '(') {
        function_len -= 2;
      }
    }

    // The hyperlink built above (if any) always points at the original,
    // absolute "file:line" span. Only the displayed text is shortened
    // here, relative to the current directory for a project source file,
    // or compacted with an ellipsis for a system/library header (see
    // FormatDisplayPath() in color.h).
    const char* const file_line_span = symbol + frame.file_line_offset;
    char display_buf[160];
    MinimalFormatter display_formatter(display_buf, sizeof(display_buf));
    const char* display_path;
    std::size_t display_path_len;
    const char* line_text;
    std::size_t line_text_len;
    if (SplitFileLineSpan(file_line_span, frame.file_line_length, &display_path,
                          &display_path_len, &line_text, &line_text_len)) {
      // Enough leading components to keep an out-of-project path (e.g. a
      // system header) recognizable as such, and enough trailing ones to
      // show the file's immediate parent directory alongside its name.
      constexpr std::size_t kDisplayPathPrefixComponents = 2;
      constexpr std::size_t kDisplayPathSuffixComponents = 2;
      char short_path[128];
      FormatDisplayPath(
          display_path, display_path_len, g_cwd, kDisplayPathPrefixComponents,
          kDisplayPathSuffixComponents, short_path, sizeof(short_path));
      display_formatter.AppendString(short_path);
      display_formatter.AppendString(":");
      display_formatter.AppendString(line_text, line_text_len);
    } else {
      display_formatter.AppendString(file_line_span, frame.file_line_length);
    }

    const ColorSpec file_spec = theme.Get(Role::kStackFile);
    const std::size_t display_len = display_formatter.num_bytes_written();
    WriteStyledField(TextAttributes{file_spec, Hyperlink(uri_pointer)}, mode,
                     g_failure_writer, display_buf, display_len);
    if (frame.file_line_offset > 0) {
      g_failure_writer(" ", 1);
      WriteStyledField(theme.Get(Role::kStackFunction), mode, g_failure_writer,
                       symbol, function_len);
    } else {
      const char* tail =
          symbol + frame.file_line_offset + frame.file_line_length;
      std::size_t tail_len = std::strlen(tail);
      if (tail_len > 0 && tail[0] == ' ') {
        // The libbacktrace/addr2line backends separate the file:line span
        // from the function name that follows it with a single space. Leave
        // that separator uncolored rather than folding it into the function
        // name's colored span.
        g_failure_writer(" ", 1);
        ++tail;
        --tail_len;
      }
      WriteStyledField(theme.Get(Role::kStackFunction), mode, g_failure_writer,
                       tail, tail_len);
    }
  } else
#endif  // defined(HAVE_SYMBOLIZE)
  {
    // A resolved-but-lineless symbol still reads as a function name. Only
    // the "(unknown)" fallback (Symbolize() failed outright) gets the
    // distinct "unresolved" role.
    const Role role =
        symbol_resolved ? Role::kStackFunction : Role::kStackUnresolved;
    WriteStyledField(theme.Get(role), mode, g_failure_writer, symbol,
                     std::strlen(symbol));
  }
  g_failure_writer("\n", 1);
}

// Invoke the default signal handler.
void InvokeDefaultSignalHandler(int signal_number) {
#ifdef HAVE_SIGACTION
  struct sigaction sig_action;
  memset(&sig_action, 0, sizeof(sig_action));
  sigemptyset(&sig_action.sa_mask);
  sig_action.sa_handler = SIG_DFL;
  sigaction(signal_number, &sig_action, nullptr);
  kill(getpid(), signal_number);
#elif defined(NGLOG_OS_WINDOWS)
  signal(signal_number, SIG_DFL);
  raise(signal_number);
#endif
}

// Set while this thread is executing the failure handler. sig_atomic_t is used
// because another signal can interrupt the handler between any two
// instructions. No other thread accesses this object.
thread_local volatile sig_atomic_t t_failure_signal_handler_entered = 0;

// Gives up the rest of the current scheduling quantum. Used only by
// threads spinning to be killed, so promptness doesn't matter. Only
// sleep() is on the POSIX async-signal-safe list, but the others used
// here are thin, state-free syscalls on every platform ng-log targets.
void RelinquishTimeSlice() {
#ifdef NGLOG_OS_WINDOWS
  SwitchToThread();
#elif defined(HAVE_SCHED_YIELD)  // !defined(NGLOG_OS_WINDOWS)
  sched_yield();
#elif defined(HAVE_NANOSLEEP)    // !defined(HAVE_SCHED_YIELD)
  const struct timespec zero_duration = {};
  nanosleep(&zero_duration, nullptr);
#else                            // !defined(HAVE_NANOSLEEP)
  sleep(1);
#endif                           // defined(NGLOG_OS_WINDOWS)
}

// True while one thread owns failure-dump generation. Other threads wait for
// that handler to terminate the process. If termination does not occur (for
// example, because a default signal disposition is ignored by PID 1), the
// owner releases the flag and a waiting thread can take over. Deliberately not
// a std::call_once() or std::once_flag: those are not async-signal-safe and a
// reentrant thread could deadlock instead of dying.
static std::atomic<bool> g_failure_signal_handler_entered{false};
// is_always_lock_free needs C++17, so ATOMIC_BOOL_LOCK_FREE == 2, its
// standard-mandated "always lock-free" equivalent, covers the C++14 floor.
#if defined(__cpp_lib_atomic_is_always_lock_free)
constexpr bool kFailureSignalHandlerEnteredIsLockFree =
    decltype(g_failure_signal_handler_entered)::is_always_lock_free;
#else   // !defined(__cpp_lib_atomic_is_always_lock_free)
constexpr bool kFailureSignalHandlerEnteredIsLockFree =
    ATOMIC_BOOL_LOCK_FREE == 2;
#endif  // defined(__cpp_lib_atomic_is_always_lock_free)
// A lock-based atomic could take a lock already held by the very thread
// its own signal interrupted, deadlocking instead of dying.
static_assert(kFailureSignalHandlerEnteredIsLockFree,
              "g_failure_signal_handler_entered must be lock-free to be "
              "async-signal-safe");

static void HandleSignal(int signal_number
#if !defined(NGLOG_OS_WINDOWS)
                         ,
                         siginfo_t* signal_info, void* ucontext
#endif
) {

  // This is the first time we enter the signal handler.  We are going to
  // do some interesting stuff from here.
  // TODO(satorux): We might want to set timeout here using alarm(), but
  // mixing alarm() and sleep() can be a bad idea.

  // First dump time info.
  DumpTimeInfo();

#if defined(HAVE_STACKTRACE) && defined(HAVE_SIGACTION)
  DumpSignalInfo(signal_number, signal_info);
#elif !defined(NGLOG_OS_WINDOWS)
  (void)signal_info;
#endif

#if !defined(NGLOG_OS_WINDOWS)
  // Get the program counter from ucontext.
  void* pc = GetPC(ucontext);
  DumpStackFrameInfo("PC: ", pc);
#endif

#ifdef HAVE_STACKTRACE
  // Get the stack traces.
  void* stack[32];
  // +1 to exclude this function.
  const int depth = GetStackTrace(stack, ARRAYSIZE(stack), 1);
  // Dump the stack traces.
  for (int i = 0; i < depth; ++i) {
    DumpStackFrameInfo("    ", stack[i]);
  }
#endif

  // *** TRANSITION ***
  //
  // BEFORE this point, all code must be async-termination-safe!
  // (See WARNING above.)
  //
  // AFTER this point, we do unsafe things, like using LOG()!
  // The process could be terminated or hung at any time.  We try to
  // do more useful things first and riskier things later.

  // Flush the logs before we do anything in case 'anything'
  // causes problems.
  FlushLogFilesUnsafe(NGLOG_INFO);

  // Kill ourself by the default signal handler. Must return afterwards.
  // signal_number stays blocked on this thread until we do, since we
  // have no SA_NODEFER, so spinning here would keep the just-reraised
  // signal pending forever instead of letting it kill the process.
  InvokeDefaultSignalHandler(signal_number);
}

// Dumps signal and stack frame information, and invokes the default
// signal handler once our job is done.
#if defined(NGLOG_OS_WINDOWS)
void FailureSignalHandler(int signal_number)
#else
void FailureSignalHandler(int signal_number, siginfo_t* signal_info,
                          void* ucontext)
#endif
{
  if (t_failure_signal_handler_entered != 0) {
    // Reentered on this thread, likely because dumping raised another signal.
    // Don't retry, just die. Must return, not spin, afterwards for the same
    // reason as in HandleSignal() above. signal_number is still blocked here
    // until we do.
    InvokeDefaultSignalHandler(signal_number);
    return;
  }
  t_failure_signal_handler_entered = 1;

  bool expected = false;
  // Retry until this thread owns failure-dump generation.
  while (!g_failure_signal_handler_entered.compare_exchange_weak(
      expected, true, std::memory_order_acquire, std::memory_order_relaxed)) {
    // Wait with loads to avoid repeated read-modify-write operations.
    while (g_failure_signal_handler_entered.load(std::memory_order_acquire)) {
      RelinquishTimeSlice();
    }
    expected = false;
  }

  HandleSignal(signal_number
#if !defined(NGLOG_OS_WINDOWS)
               ,
               signal_info, ucontext
#endif
  );

  // Normally the re-raised signal terminates the process after this handler
  // returns. Release ownership anyway because some default dispositions can be
  // ignored, most notably SIGTERM for PID 1. No pointer to thread-local storage
  // is published, so a thread that exits after returning cannot leave dangling
  // shared state.
  g_failure_signal_handler_entered.store(false, std::memory_order_release);
  t_failure_signal_handler_entered = 0;
}

}  // namespace

bool IsFailureSignalHandlerInstalled() {
#ifdef HAVE_SIGACTION
  // TODO(andschwa): Return kFailureSignalHandlerInstalled?
  struct sigaction sig_action;
  memset(&sig_action, 0, sizeof(sig_action));
  sigemptyset(&sig_action.sa_mask);
  sigaction(SIGABRT, nullptr, &sig_action);
  return sig_action.sa_sigaction == &FailureSignalHandler;
#elif defined(NGLOG_OS_WINDOWS)
  return kFailureSignalHandlerInstalled;
#endif  // HAVE_SIGACTION
  return false;
}

void InstallFailureSignalHandler() {
  // Decide once, now, whether/how stack frames dumped to stderr may be
  // colorized/hyperlinked, and resolve the local hostname and current
  // directory: getenv(), isatty(), gethostname(), and getcwd() are not
  // async-signal-safe, so none of them can be called once a signal has
  // actually fired.
  g_stderr_color_mode =
      FLAGS_colorlogtostderr ? StreamColorMode(stderr) : ColorMode::kNone;
  g_stderr_hyperlinks = StreamSupportsHyperlinks(stderr);
  ComputeHostname();
  ComputeCwd();
#ifdef HAVE_SIGACTION
  // Build the sigaction struct.
  struct sigaction sig_action;
  memset(&sig_action, 0, sizeof(sig_action));
  sigemptyset(&sig_action.sa_mask);
  sig_action.sa_flags |= SA_SIGINFO;
  sig_action.sa_sigaction = &FailureSignalHandler;

  for (auto kFailureSignal : kFailureSignals) {
    CHECK_ERR(sigaction(kFailureSignal.number, &sig_action, nullptr));
  }
#elif defined(NGLOG_OS_WINDOWS)
  for (size_t i = 0; i < ARRAYSIZE(kFailureSignals); ++i) {
    CHECK_NE(signal(kFailureSignals[i].number, &FailureSignalHandler), SIG_ERR);
  }
  kFailureSignalHandlerInstalled = true;
#endif  // HAVE_SIGACTION
}

void InstallFailureWriter(void (*writer)(const char* data, size_t size)) {
#if defined(HAVE_SIGACTION) || defined(NGLOG_OS_WINDOWS)
  g_failure_writer = writer;
#endif  // HAVE_SIGACTION
}

}  // namespace nglog
