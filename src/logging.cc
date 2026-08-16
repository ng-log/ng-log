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

#define _GNU_SOURCE 1  // needed for O_NOFOLLOW and pread()/pwrite()

#include "ng-log/logging.h"

#include <fcntl.h>
#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cctype>  // for std::isspace
#include <cerrno>  // for errno
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <regex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "config.h"
#include "internal/lock_metrics.h"
#include "internal/log_cleaner.h"
#include "internal/source_location.h"
#include "internal/style_recorder.h"
#include "internal/styled_output.h"
#include "internal/terminal_capabilities.h"
#include "internal/theme.h"
#include "ng-log/platform.h"
#include "ng-log/raw_logging.h"
#include "stacktrace.h"
#include "utilities.h"

#ifdef NGLOG_OS_WINDOWS
#  include "windows/dirent.h"
#else
#  include <dirent.h>  // for automatic removal of old logs
#endif

#if defined(HAVE__CHSIZE_S) || defined(NGLOG_OS_WINDOWS)
#  include <io.h>  // for truncate log file
#endif
#ifdef NGLOG_OS_WINDOWS
#  include <windows.h>
#endif
#ifdef HAVE_PWD_H
#  include <pwd.h>
#endif
#ifdef HAVE_SYS_UTSNAME_H
#  include <sys/utsname.h>  // For uname.
#endif
#ifdef HAVE_SYSLOG_H
#  include <syslog.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#ifndef HAVE_MODE_T
typedef int mode_t;
#endif

using std::dec;
using std::hex;
using std::min;
using std::ostream;
using std::ostringstream;
using std::setfill;
using std::setw;
using std::string;
using std::vector;

using std::fclose;
using std::fflush;
using std::FILE;
using std::fprintf;
using std::fputs;
using std::fwrite;
using std::perror;

#ifdef __QNX__
using std::fdopen;
#endif

// TODO(hamaji): consider windows
enum { PATH_SEPARATOR = '/' };

#ifndef HAVE_PREAD
static ssize_t pread(int fd, void* buf, size_t count, off_t offset) {
  off_t orig_offset = lseek(fd, 0, SEEK_CUR);
  if (orig_offset == (off_t)-1) return -1;
  if (lseek(fd, offset, SEEK_CUR) == (off_t)-1) return -1;
  ssize_t len = read(fd, buf, count);
  if (len < 0) return len;
  if (lseek(fd, orig_offset, SEEK_SET) == (off_t)-1) return -1;
  return len;
}
#endif  // !HAVE_PREAD

#ifndef HAVE_PWRITE
static ssize_t pwrite(int fd, void* buf, size_t count, off_t offset) {
  off_t orig_offset = lseek(fd, 0, SEEK_CUR);
  if (orig_offset == (off_t)-1) return -1;
  if (lseek(fd, offset, SEEK_CUR) == (off_t)-1) return -1;
  ssize_t len = write(fd, buf, count);
  if (len < 0) return len;
  if (lseek(fd, orig_offset, SEEK_SET) == (off_t)-1) return -1;
  return len;
}
#endif  // !HAVE_PWRITE

static void GetHostName(string* hostname) {
#if defined(HAVE_SYS_UTSNAME_H)
  struct utsname buf;
  if (uname(&buf) < 0) {
    // ensure null termination on failure
    *buf.nodename = '\0';
  }
  *hostname = buf.nodename;
#elif defined(NGLOG_OS_WINDOWS)
  char buf[MAX_COMPUTERNAME_LENGTH + 1];
  DWORD len = MAX_COMPUTERNAME_LENGTH + 1;
  if (GetComputerNameA(buf, &len)) {
    *hostname = buf;
  } else {
    hostname->clear();
  }
#else
#  warning There is no way to retrieve the host name.
  *hostname = "(unknown)";
#endif
}

namespace nglog {

using namespace internal;

NGLOG_NO_EXPORT
std::string StrError(int err);

// Maps a log severity to the Role used to colorize it (see color.h).
// DefaultTheme() leaves Role::kLogInfo at Color::kDefault, i.e. uncolored.
static Role SeverityToRole(LogSeverity severity) {
  switch (severity) {
    case NGLOG_INFO:
      return Role::kLogInfo;
    case NGLOG_WARNING:
      return Role::kLogWarning;
    case NGLOG_ERROR:
      return Role::kLogError;
    case NGLOG_FATAL:
      return Role::kLogFatal;
  }

  // should never get here.
  NGLOG_UNREACHABLE;
}

// Safely get max_log_size. A value of 0 is overridden to the 1 MB minimum.
// Values large enough to overflow the "MaxLogSize() << 20" byte computation
// are capped to the 4095 MB maximum instead of being silently reduced to the
// minimum, which would produce surprisingly tiny log files.
static uint32 MaxLogSize() {
  constexpr uint32 max_megabytes = 4095;  // 4095 << 20 fits in a uint32.
  if (FLAGS_max_log_size == 0) {
    return 1;
  }
  return FLAGS_max_log_size < max_megabytes ? FLAGS_max_log_size
                                            : max_megabytes;
}

// An arbitrary limit on the length of a single log message.  This
// is so that streaming can be done more efficiently.
const size_t LogMessage::kMaxLogMessageLen = 30000;

namespace internal {
struct LogMessageData {
  LogMessageData();

  int preserved_errno_;  // preserved errno
  // Buffer space; contains complete message text.
  char message_text_[LogMessage::kMaxLogMessageLen + 1];
  LogMessage::LogStream stream_;
  StyleRecorder styles_;
  LogSeverity severity_;  // What level is this LogMessage logged at?
  int line_;              // line number where logging call is.
  void (LogMessage::*send_method_)();  // Call this in destructor to send
  union {  // At most one of these is used: union to keep the size low.
    LogSink* sink_;  // nullptr or sink to send message to
    std::vector<std::string>*
        outvec_;            // nullptr or vector to push message onto
    std::string* message_;  // nullptr or string to write message into
  };
  size_t num_prefix_chars_;     // # of chars of prefix in this message
  size_t num_chars_to_log_;     // # of chars of msg to send to log
  size_t num_chars_to_syslog_;  // # of chars of msg to send to syslog
  const char* basename_;        // basename of file that called LOG
  const char* fullname_;        // fullname of file that called LOG
  bool has_been_flushed_;       // false => data has not been flushed
  bool first_fatal_;            // true => this was first fatal msg
  std::thread::id thread_id_;

  // Byte lengths of the individual fields making up the default log
  // prefix ("Eyyyymmdd hh:mm:ss.uuuuuu tid file:line] "), with no
  // separator between the severity character and the timestamp, and a
  // single space separating every other field: the severity character
  // starts at offset 0, the timestamp follows at prefix_severity_len_,
  // the thread id follows at prefix_severity_len_ + prefix_timestamp_len_
  // + 1, and "file:line" follows that at prefix_severity_len_ +
  // prefix_timestamp_len_ + 1 + prefix_threadid_len_ + 1. Lets
  // ColoredWriteToStderrOrStdout() colorize each field on its own,
  // without having to reparse the composed prefix text. Only meaningful
  // when has_default_prefix_ is true: a caller-supplied prefix formatter
  // (see InstallPrefixFormatter()) has no known sub-structure to color
  // this way.
  size_t prefix_severity_len_;
  size_t prefix_timestamp_len_;
  size_t prefix_threadid_len_;
  size_t prefix_fileline_len_;
  bool has_default_prefix_;

  LogMessageData(const LogMessageData&) = delete;
  LogMessageData& operator=(const LogMessageData&) = delete;
};
}  // namespace internal

// A mutex that allows only one thread to log at a time, to keep things from
// getting jumbled.  Some other very uncommon logging operations (like
// changing the destination file for log messages of a given severity) also
// lock this mutex.  Please be sure that anybody who might possibly need to
// lock it does so.
static internal::LogMutex log_mutex;

// Number of messages sent at each severity.  Under log_mutex.
std::atomic<int64> LogMessage::num_messages_[NUM_SEVERITIES]{};

// Globally disable log writing (if disk is full)
static std::atomic<bool> stop_writing{false};

const char* const LogSeverityNames[] = {"INFO", "WARNING", "ERROR", "FATAL"};

// Has the user called SetExitOnDFatal(true)?
static std::atomic<bool> exit_on_dfatal{true};

const char* GetLogSeverityName(LogSeverity severity) {
  return LogSeverityNames[severity];
}

static bool SendEmailInternal(const char* dest, const char* subject,
                              const char* body, bool use_logging);

base::Logger::~Logger() = default;

namespace {

constexpr std::intmax_t kSecondsInDay = 60 * 60 * 24;
constexpr std::size_t kPrefixNumberBufferSize = 32;
constexpr std::size_t kPrefixYearWidth = 4;
constexpr std::size_t kPrefixMonthWidth = 2;
constexpr std::size_t kPrefixDayWidth = 2;
constexpr std::size_t kPrefixHourWidth = 2;
constexpr std::size_t kPrefixMinuteWidth = 2;
constexpr std::size_t kPrefixSecondWidth = 2;
constexpr std::size_t kPrefixMicrosecondWidth = 6;
constexpr std::size_t kPrefixThreadIdWidth = 5;

#ifdef NGLOG_OS_WINDOWS
constexpr std::uint64_t kWindowsDwordBits = std::numeric_limits<DWORD>::digits;
constexpr std::uint64_t kLogFileLockOffset = std::uint64_t{1}
                                             << kWindowsDwordBits;
constexpr DWORD kLogFileLockLength = 1;

std::string WindowsErrorMessage(DWORD error) {
  std::string result = FormatWindowsMessage(error);

  if (result.empty()) {
    return "Windows error " + std::to_string(error);
  }

  return TrimTrailingCRLF(std::move(result));
}
#endif

void AppendPrefixText(LogMessage::LogStream& stream, const char* text) {
  stream.append(text, std::strlen(text));
}

template <typename Integer>
void AppendPrefixNumber(LogMessage::LogStream& stream, Integer value,
                        std::size_t width = 0) {
  std::array<char, kPrefixNumberBufferSize> number;
  using UnsignedInteger = std::make_unsigned_t<Integer>;
  constexpr UnsignedInteger kPrefixDecimalBase = 10;
  const bool negative = value < 0;
  UnsignedInteger magnitude =
      negative ? static_cast<UnsignedInteger>(-(value + 1)) + 1
               : static_cast<UnsignedInteger>(value);
  char* end = number.data() + number.size();
  char* begin = end;
  do {
    const UnsignedInteger digit = magnitude % kPrefixDecimalBase;
    *--begin = static_cast<char>('0' + digit);
    magnitude /= kPrefixDecimalBase;
  } while (magnitude != 0);
  if (negative) *--begin = '-';

  const std::size_t digits = static_cast<std::size_t>(end - begin);
  if (digits < width) {
    // number has ample unused space ahead of begin (kPrefixNumberBufferSize
    // comfortably exceeds any width in use here), so the zero padding can be
    // written directly in front of the digits already extracted above,
    // instead of copying them into a second buffer.
    const std::size_t zeroes = width - digits;
    begin -= zeroes;
    std::fill_n(begin, zeroes, '0');
  }
  stream.append(begin, std::max(digits, width));
}

void AppendDefaultPrefix(internal::LogMessageData& data, LogSeverity severity,
                         const LogMessageTime& time) {
  LogMessage::LogStream& stream = data.stream_;
  stream.append(LogSeverityNames[severity], 1);
  data.prefix_severity_len_ = stream.pcount();
  if (FLAGS_log_year_in_prefix) {
    AppendPrefixNumber(stream, 1900 + time.year(), kPrefixYearWidth);
  }
  AppendPrefixNumber(stream, 1 + time.month(), kPrefixMonthWidth);
  AppendPrefixNumber(stream, time.day(), kPrefixDayWidth);
  AppendPrefixText(stream, " ");
  AppendPrefixNumber(stream, time.hour(), kPrefixHourWidth);
  AppendPrefixText(stream, ":");
  AppendPrefixNumber(stream, time.min(), kPrefixMinuteWidth);
  AppendPrefixText(stream, ":");
  AppendPrefixNumber(stream, time.sec(), kPrefixSecondWidth);
  AppendPrefixText(stream, ".");
  AppendPrefixNumber(stream, time.usec(), kPrefixMicrosecondWidth);
  data.prefix_timestamp_len_ = stream.pcount() - data.prefix_severity_len_;
  AppendPrefixText(stream, " ");
  stream << std::setw(kPrefixThreadIdWidth) << data.thread_id_;
  data.prefix_threadid_len_ = stream.pcount() - data.prefix_severity_len_ -
                              data.prefix_timestamp_len_ - 1;
  AppendPrefixText(stream, " ");
  AppendPrefixText(stream, data.basename_);
  AppendPrefixText(stream, ":");
  AppendPrefixNumber(stream, data.line_);
  data.prefix_fileline_len_ = stream.pcount() - data.prefix_severity_len_ -
                              data.prefix_timestamp_len_ - 1 -
                              data.prefix_threadid_len_ - 1;
  AppendPrefixText(stream, "] ");
  data.has_default_prefix_ = true;
}

// Optional user-configured callback to print custom prefixes.
class PrefixFormatter {
 public:
  PrefixFormatter(PrefixFormatterCallback callback, void* data) noexcept
      : version{V2}, callback_v2{callback}, data{data} {}

  void operator()(std::ostream& s, const LogMessage& message) const {
    switch (version) {
      case V2:
        callback_v2(s, message, data);
        break;
    }
  }

  PrefixFormatter(const PrefixFormatter& other) = delete;
  PrefixFormatter& operator=(const PrefixFormatter& other) = delete;

 private:
  enum Version { V2 } version;
  union {
    PrefixFormatterCallback callback_v2;
  };
  // User-provided data to pass to the callback:
  void* data;
};

std::unique_ptr<PrefixFormatter> g_prefix_formatter;

// Encapsulates all file-system related state
class LogFileObject : public base::Logger {
 public:
  LogFileObject(LogSeverity severity, const char* base_filename);
  ~LogFileObject() override;

  void Write(bool force_flush,  // Should we force a flush here?
             const std::chrono::system_clock::time_point&
                 timestamp,  // Timestamp for this entry
             const char* message, size_t message_len) override;

  // Called while the global logging mutex serializes the default logger.
  void WriteUnlocked(bool force_flush,
                     const std::chrono::system_clock::time_point& timestamp,
                     const char* message, size_t message_len,
                     std::unique_lock<internal::FileMutex>* lock);

  // Configuration options
  void SetBasename(const char* basename);
  void SetExtension(const char* ext);
  void SetSymlinkBasename(const char* symlink_basename);

  // Normal flushing routine
  void Flush() override;

  // It is the actual file length for the system loggers,
  // i.e., INFO, ERROR, etc.
  uint32 LogSize() override {
    std::lock_guard<internal::FileMutex> l{mutex_};
    return static_cast<uint32>(file_length_);
  }

  // Internal flush routine.  Exposed so that FlushLogFilesUnsafe()
  // can avoid grabbing a lock.  Usually Flush() calls it after
  // acquiring lock_.
  void FlushUnlocked(const std::chrono::system_clock::time_point& now);

 private:
  static const uint32 kRolloverAttemptFrequency = 0x20;

  internal::FileMutex mutex_;
  bool base_filename_selected_;
  string base_filename_;
  string symlink_basename_;
  string filename_extension_;  // option users can specify (eg to add port#)
  std::unique_ptr<FILE> file_;
  LogSeverity severity_;
  std::size_t bytes_since_flush_{0};
  std::size_t dropped_mem_length_{0};
  std::size_t file_length_{0};
  unsigned int rollover_attempt_;
  std::chrono::system_clock::time_point
      next_flush_time_;  // cycle count at which to flush log
  std::chrono::system_clock::time_point start_time_;
#ifdef NGLOG_OS_WINDOWS
  std::string create_error_message_;
  std::string create_filename_;
#endif

  // Actually create a logfile using the value of base_filename_ and the
  // optional argument time_pid_string
  // REQUIRES: lock_ is held
  bool CreateLogfile(const string& time_pid_string);
};

}  // namespace

class LogDestination {
 public:
  friend class LogMessage;
  friend void ReprintFatalMessage();
  friend base::Logger* base::GetLogger(LogSeverity);
  friend void base::SetLogger(LogSeverity, base::Logger*);

  // These methods are just forwarded to by their global versions.
  static void SetLogDestination(LogSeverity severity,
                                const char* base_filename);
  static void SetLogSymlink(LogSeverity severity, const char* symlink_basename);
  static void AddLogSink(LogSink* destination);
  static void RemoveLogSink(LogSink* destination);
  static void SetLogFilenameExtension(const char* filename_extension);
  static void SetStderrLogging(LogSeverity min_severity);
  static void SetEmailLogging(LogSeverity min_severity, const char* addresses);
  static void LogToStderr();
  // Flush all log files that are at least at the given severity level
  static void FlushLogFiles(int min_severity);
  static void FlushLogFilesUnsafe(int min_severity);

  // we set the maximum size of our packet to be 1400, the logic being
  // to prevent fragmentation.
  // Really this number is arbitrary.
  static const int kNetworkBytes = 1400;

  static const string& hostname();

  static void DeleteLogDestinations();
  LogDestination(LogSeverity severity, const char* base_filename);

 private:
#ifdef NGLOG_ENABLE_LOCK_METRICS
  using SinkMutex = internal::MetricsSharedMutex<internal::LockKind::kSink>;
  using SinkLock = std::lock_guard<SinkMutex>;
#else
#  if defined(__cpp_lib_shared_mutex) && (__cpp_lib_shared_mutex >= 201505L)
  // Use untimed shared mutex
  using SinkMutex = std::shared_mutex;
  using SinkLock = std::lock_guard<SinkMutex>;
#  else   // !(defined(__cpp_lib_shared_mutex) && (__cpp_lib_shared_mutex >=
          // 201505L)) Fallback to timed shared mutex
  using SinkMutex = std::shared_timed_mutex;
  using SinkLock = std::unique_lock<SinkMutex>;
#  endif  // defined(__cpp_lib_shared_mutex) && (__cpp_lib_shared_mutex >=
          // 201505L)
#endif    // NGLOG_ENABLE_LOCK_METRICS

  friend std::default_delete<LogDestination>;
  ~LogDestination();

  // Take a log message of a particular severity and log it to stderr
  // iff it's of a high enough severity to deserve it.
  static void MaybeLogToStderr(const internal::LogMessageData& data);

  // Take a log message of a particular severity and log it to email
  // iff it's of a high enough severity to deserve it.
  static void MaybeLogToEmail(LogSeverity severity, const char* message,
                              size_t len);
  // Take a log message of a particular severity and log it to a file
  // iff the base filename is not "" (which means "don't log to me")
  static void MaybeLogToLogfile(
      LogSeverity severity,
      const std::chrono::system_clock::time_point& timestamp,
      const char* message, size_t len);
  static void MaybeLogToLogfileLocked(
      LogSeverity severity,
      const std::chrono::system_clock::time_point& timestamp,
      const char* message, size_t len);
  // Take a log message of a particular severity and log it to the file
  // for that severity and also for all files with severity less than
  // this severity.
  static void LogToAllLogfiles(
      LogSeverity severity,
      const std::chrono::system_clock::time_point& timestamp,
      const char* message, size_t len);
  static void LogToAllLogfilesLocked(
      LogSeverity severity,
      const std::chrono::system_clock::time_point& timestamp,
      const char* message, size_t len);

  // Send logging info to all registered sinks.
  static bool LogToSinks(LogSeverity severity, const char* full_filename,
                         const char* base_filename, int line,
                         const LogMessageTime& time, const char* message,
                         size_t message_len);

  static std::shared_ptr<const std::vector<LogSink*>> GetSinksForCall();
  static void EndSinkCall(LogSink* sink);
  static void WaitForSinkCalls(LogSink* sink);
  static void WaitForAllSinkCalls();

  class SinkCallGuard {
   public:
    explicit SinkCallGuard(LogSink* sink) : sink_(sink) {}
    ~SinkCallGuard() { LogDestination::EndSinkCall(sink_); }

   private:
    LogSink* sink_;
  };

  // Wait for all registered sinks via WaitTillSent
  // including the optional one in "data".
  static void WaitForSinks(internal::LogMessageData* data,
                           bool wait_for_registered_sinks);

  static LogDestination* log_destination(LogSeverity severity);

  base::Logger* GetLoggerImpl() const { return logger_; }
  void SetLoggerImpl(base::Logger* logger);
  void ResetLoggerImpl() { SetLoggerImpl(&fileobject_); }

  LogFileObject fileobject_;
  base::Logger* logger_;  // Either &fileobject_, or wrapper around it

  static std::unique_ptr<LogDestination> log_destinations_[NUM_SEVERITIES];
  static std::underlying_type_t<LogSeverity> email_logging_severity_;
  static string addresses_;
  static string hostname_;
  static std::once_flag hostname_once_;

  // arbitrary global logging destinations.
  static std::shared_ptr<const vector<LogSink*>> sinks_;
  static std::atomic<bool> sinks_present_;

  // Protects the vector sinks_,
  // but not the LogSink objects its elements reference.
  static SinkMutex sink_mutex_;
  static std::mutex sink_call_mutex_;
  static std::condition_variable sink_call_cond_;
  static std::unordered_map<LogSink*, size_t> sink_call_counts_;

  // Disallow
  LogDestination(const LogDestination&) = delete;
  LogDestination& operator=(const LogDestination&) = delete;
};

// Errors do not get logged to email by default.
std::underlying_type_t<LogSeverity> LogDestination::email_logging_severity_ =
    99999;

string LogDestination::addresses_;
string LogDestination::hostname_;

std::shared_ptr<const vector<LogSink*>> LogDestination::sinks_;
std::atomic<bool> LogDestination::sinks_present_{false};
LogDestination::SinkMutex LogDestination::sink_mutex_;
std::mutex LogDestination::sink_call_mutex_;
std::condition_variable LogDestination::sink_call_cond_;
std::unordered_map<LogSink*, size_t> LogDestination::sink_call_counts_;
thread_local std::unordered_map<LogSink*, size_t> sink_call_counts_for_thread;
std::once_flag LogDestination::hostname_once_;

/* static */
const string& LogDestination::hostname() {
  std::call_once(hostname_once_, [] {
    GetHostName(&hostname_);
    if (hostname_.empty()) {
      hostname_ = "(unknown)";
    }
  });
  return hostname_;
}

LogDestination::LogDestination(LogSeverity severity, const char* base_filename)
    : fileobject_(severity, base_filename), logger_(&fileobject_) {}

LogDestination::~LogDestination() { ResetLoggerImpl(); }

void LogDestination::SetLoggerImpl(base::Logger* logger) {
  if (logger_ == logger) {
    // Prevent releasing currently held sink on reset
    return;
  }

  if (logger_ && logger_ != &fileobject_) {
    // Delete user-specified logger set via SetLogger().
    delete logger_;
  }
  logger_ = logger;
}

inline void LogDestination::FlushLogFilesUnsafe(int min_severity) {
  // assume we have the log_mutex or we simply don't care
  // about it
  std::for_each(std::next(std::begin(log_destinations_), min_severity),
                std::end(log_destinations_),
                [now = std::chrono::system_clock::now()](
                    std::unique_ptr<LogDestination>& log) {
                  if (log != nullptr) {
                    // Flush the base fileobject_ logger directly instead of
                    // going through any wrappers to reduce chance of deadlock.
                    log->fileobject_.FlushUnlocked(now);
                  }
                });
}

inline void LogDestination::FlushLogFiles(int min_severity) {
  // Prevent any subtle race conditions by wrapping a mutex lock around
  // all this stuff.
  std::lock_guard<::nglog::internal::LogMutex> l{log_mutex};
  for (int i = min_severity; i < NUM_SEVERITIES; i++) {
    LogDestination* log = log_destination(static_cast<LogSeverity>(i));
    if (log != nullptr) {
      log->logger_->Flush();
    }
  }
}

inline void LogDestination::SetLogDestination(LogSeverity severity,
                                              const char* base_filename) {
  // Prevent any subtle race conditions by wrapping a mutex lock around
  // all this stuff.
  std::lock_guard<::nglog::internal::LogMutex> l{log_mutex};
  log_destination(severity)->fileobject_.SetBasename(base_filename);
}

inline void LogDestination::SetLogSymlink(LogSeverity severity,
                                          const char* symlink_basename) {
  CHECK_GE(severity, 0);
  CHECK_LT(severity, NUM_SEVERITIES);
  std::lock_guard<::nglog::internal::LogMutex> l{log_mutex};
  log_destination(severity)->fileobject_.SetSymlinkBasename(symlink_basename);
}

inline void LogDestination::AddLogSink(LogSink* destination) {
  // Prevent any subtle race conditions by wrapping a mutex lock around
  // all this stuff.
  SinkLock l{sink_mutex_};
  std::vector<LogSink*> sinks = sinks_ ? *sinks_ : std::vector<LogSink*>{};
  sinks.push_back(destination);
  sinks_ = std::make_shared<const std::vector<LogSink*>>(std::move(sinks));
  sinks_present_.store(true, std::memory_order_release);
}

inline void LogDestination::RemoveLogSink(LogSink* destination) {
  // Prevent any subtle race conditions by wrapping a mutex lock around
  // all this stuff.
  {
    SinkLock l{sink_mutex_};
    // This doesn't keep the sinks in order, but who cares?
    if (sinks_) {
      std::vector<LogSink*> sinks = *sinks_;
      sinks.erase(std::remove(sinks.begin(), sinks.end(), destination),
                  sinks.end());
      sinks_ = std::make_shared<const std::vector<LogSink*>>(std::move(sinks));
      sinks_present_.store(!sinks_->empty(), std::memory_order_release);
    }
  }
  WaitForSinkCalls(destination);
}

inline void LogDestination::SetLogFilenameExtension(const char* ext) {
  // Prevent any subtle race conditions by wrapping a mutex lock around
  // all this stuff.
  std::lock_guard<::nglog::internal::LogMutex> l{log_mutex};
  for (int severity = 0; severity < NUM_SEVERITIES; ++severity) {
    log_destination(static_cast<LogSeverity>(severity))
        ->fileobject_.SetExtension(ext);
  }
}

inline void LogDestination::SetStderrLogging(LogSeverity min_severity) {
  // Prevent any subtle race conditions by wrapping a mutex lock around
  // all this stuff.
  std::lock_guard<internal::LogMutex> l{log_mutex};
  FLAGS_stderrthreshold = min_severity;
}

inline void LogDestination::LogToStderr() {
  // *Don't* put this stuff in a mutex lock, since SetStderrLogging &
  // SetLogDestination already do the locking!
  SetStderrLogging(NGLOG_INFO);  // thus everything is "also" logged to stderr
  for (int i = 0; i < NUM_SEVERITIES; ++i) {
    SetLogDestination(static_cast<LogSeverity>(i),
                      "");  // "" turns off logging to a logfile
  }
}

inline void LogDestination::SetEmailLogging(LogSeverity min_severity,
                                            const char* addresses) {
  // Prevent any subtle race conditions by wrapping a mutex lock around
  // all this stuff.
  std::lock_guard<internal::LogMutex> l{log_mutex};
  LogDestination::email_logging_severity_ = min_severity;
  LogDestination::addresses_ = addresses;
}

// A minimal adapter satisfying color.h's "Formatter" concept
// (AppendString(const char*)), so WithColor()/Hyperlink::Wrap() can be used
// against a plain FILE* stream. Only used to emit the small, fixed
// escape/hyperlink sequences themselves: the colored content is always
// written straight to "output" via fwrite() inside the body lambda they
// wrap, not through this adapter.
namespace {
struct FileFormatter {
  FILE* output;
  void AppendString(const char* s) { fputs(s, output); }
};
}  // namespace

// Writes a single field of "len" bytes starting at "text", bracketed with
// the color for "spec" according to "mode" (see WriteColoredField() in
// color.h for the analogous, but stderr-only and pluggable-writer-safe,
// helper the crash path uses). Unlike that one, this may target either
// stdout or stderr, and writes straight to "output": logging.cc's regular
// output path has no pluggable, content-only sink to keep clean of escape
// sequences the way signalhandler.cc's g_failure_writer does.
static void WriteTerminalField(FILE* output, FileFormatter& formatter,
                               ColorMode mode, ColorSpec spec, const char* text,
                               size_t len) {
  if (len == 0) {
    return;
  }
  if (mode == ColorMode::kAnsi) {
    WithColor(formatter, spec, true,
              [text, len, output] { fwrite(text, len, 1, output); });
    return;
  }
#ifdef NGLOG_OS_WINDOWS
  if (mode == ColorMode::kLegacyConsole) {
    WithLegacyConsoleAttribute(
        output, spec, [text, len, output] { fwrite(text, len, 1, output); });
    return;
  }
#endif  // NGLOG_OS_WINDOWS
  fwrite(text, len, 1, output);
}

static void WriteStyledLogField(FILE* output, FileFormatter& formatter,
                                ColorMode mode,
                                const TextAttributes& attributes,
                                bool hyperlinks_enabled, const char* text,
                                size_t len) {
  const auto write_line = [&formatter, output, mode, &attributes,
                           hyperlinks_enabled](const char* line_text,
                                               size_t line_len) {
    const auto write_colored = [&formatter, output, mode, &attributes,
                                line_text, line_len] {
      WriteTerminalField(output, formatter, mode, attributes.color, line_text,
                         line_len);
    };
    if (hyperlinks_enabled && attributes.hyperlink.uri() != nullptr) {
      attributes.hyperlink.Wrap(formatter, write_colored);
    } else {
      write_colored();
    }
  };
  const auto write_newline = [output](const char* newline, size_t newline_len) {
    fwrite(newline, newline_len, 1, output);
  };
  WriteTextByLines(text, len, write_line, write_newline);
}

#ifdef NGLOG_OS_WINDOWS
constexpr std::size_t kLogFileUriBufferSize = 1024;

static void WriteLogFileCreationError(const std::string& filename,
                                      const std::string& error_message) {
  const ColorMode mode =
      FLAGS_colorlogtostderr ? StreamColorMode(stderr) : ColorMode::kNone;
  const bool hyperlinks_enabled = mode == ColorMode::kAnsi &&
                                  FLAGS_symbolize_hyperlinks &&
                                  StreamSupportsHyperlinks(stderr);

  char uri[kLogFileUriBufferSize];
  const char* uri_pointer = nullptr;
  if (hyperlinks_enabled &&
      BuildFileUri(filename.c_str(), filename.size(),
                   FLAGS_symbolize_file_base_path.c_str(),
                   CachedHostname().c_str(), uri, kLogFileUriBufferSize)) {
    uri_pointer = uri;
  }

  FileFormatter formatter{stderr};
  constexpr char kPrefix[] = "Could not create log file '";
  constexpr char kSeparator[] = "': ";
  std::fwrite(kPrefix, sizeof(kPrefix) - 1, 1, stderr);
  WriteStyledLogField(stderr, formatter, mode,
                      TextAttributes{DefaultTheme().Get(Role::kStackFile),
                                     Hyperlink(uri_pointer)},
                      hyperlinks_enabled, filename.c_str(), filename.size());
  std::fwrite(kSeparator, sizeof(kSeparator) - 1, 1, stderr);
  WriteStyledLogField(
      stderr, formatter, mode,
      TextAttributes{DefaultTheme().Get(Role::kErrnoMessage), Hyperlink()},
      false, error_message.c_str(), error_message.size());
  std::fputc('\n', stderr);
}
#endif

static void WriteStyledLogBody(FILE* output, FileFormatter& formatter,
                               ColorMode mode,
                               const internal::StyleRecorder& styles,
                               const ColorSpec& base_attributes,
                               bool hyperlinks_enabled, const char* message,
                               size_t begin, size_t end) {
  size_t cursor = begin;
  for (std::size_t index = 0; index < styles.size(); ++index) {
    const internal::StyleRecorder::Span& span = styles.span(index);
    if (span.end <= begin) {
      continue;
    }
    if (span.begin >= end) {
      break;
    }

    const size_t span_begin = std::max(span.begin, begin);
    const size_t span_end = std::min(span.end, end);
    if (span_begin > cursor) {
      WriteStyledLogField(
          output, formatter, mode, TextAttributes{base_attributes, Hyperlink()},
          hyperlinks_enabled, message + cursor, span_begin - cursor);
    }
    if (span_end > span_begin) {
      WriteStyledLogField(output, formatter, mode, span.attributes,
                          hyperlinks_enabled, message + span_begin,
                          span_end - span_begin);
      cursor = span_end;
    }
  }
  if (cursor < end) {
    WriteStyledLogField(output, formatter, mode,
                        TextAttributes{base_attributes, Hyperlink()},
                        hyperlinks_enabled, message + cursor, end - cursor);
  }
}

// Unlike stderr, stdout is typically buffered by the C runtime, so honor
// FLAGS_logbuflevel the same way file logging does; otherwise messages can be
// held back indefinitely even with buffering disabled
// (--logbuflevel=-1).
static void MaybeFlushStdout(FILE* output, LogSeverity severity) {
  if (output == stdout && severity > FLAGS_logbuflevel) {
    fflush(output);
  }
}

// Writes "message"/"len" to "output", colored as a single block by
// "severity" (the whole-line coloring logging.cc used before per-field
// prefix coloring was added). Used whenever a caller doesn't have a
// LogMessageData to color the individual prefix fields from: a custom
// prefix formatter may be installed (see InstallPrefixFormatter()), whose
// output has no structure logging.cc knows about, or there may be no
// LogMessage backing the call at all (e.g. LogToAllLogfiles() replaying a
// previously stored FATAL message).
static void ColoredWriteToStderrOrStdout(FILE* output, LogSeverity severity,
                                         const char* message, size_t len) {
  const bool is_stdout = (output == stdout);
  const bool want_color = (!is_stdout && FLAGS_colorlogtostderr) ||
                          (is_stdout && FLAGS_colorlogtostdout);
  const ColorMode mode =
      want_color ? StreamColorMode(output) : ColorMode::kNone;

  if (mode == ColorMode::kNone) {
    fwrite(message, len, 1, output);
    MaybeFlushStdout(output, severity);
    return;
  }

  const ColorSpec spec = DefaultTheme().Get(SeverityToRole(severity));
  const bool colored = spec.foreground != Color::kDefault ||
                       spec.background != Color::kDefault ||
                       spec.style != TextStyle::kNone;

  if (!colored) {
    fwrite(message, len, 1, output);
    MaybeFlushStdout(output, severity);
    return;
  }

  FileFormatter formatter{output};
  WriteStyledLogField(output, formatter, mode,
                      TextAttributes{spec, Hyperlink()}, false, message, len);
  MaybeFlushStdout(output, severity);
}

static void ColoredWriteToStdout(LogSeverity severity, const char* message,
                                 size_t len) {
  FILE* output = stdout;
  // We also need to send logs to the stderr when the severity is
  // higher or equal to the stderr threshold.
  if (severity >= FLAGS_stderrthreshold) {
    output = stderr;
  }
  ColoredWriteToStderrOrStdout(output, severity, message, len);
}

static void ColoredWriteToStderr(LogSeverity severity, const char* message,
                                 size_t len) {
  ColoredWriteToStderrOrStdout(stderr, severity, message, len);
}

// As ColoredWriteToStderrOrStdout(FILE*, LogSeverity, const char*,
// size_t) above, but colors the severity/timestamp, thread id, and
// file:line fields of "data"'s prefix separately, matching how the
// crash handler colors the analogous fields of a stack trace, rather
// than as part of one whole-line block. The message body keeps the
// existing whole-block, severity-based coloring. This is usable only when
// a LogMessageData with an already-composed default prefix is available.
// See ColoredWriteToStderrOrStdout() above for the fallback used
// otherwise.
static void ColoredWriteToStderrOrStdoutWithFields(
    FILE* output, const internal::LogMessageData& data) {
  if (!data.has_default_prefix_) {
    const bool is_stdout = (output == stdout);
    const bool want_color = (!is_stdout && FLAGS_colorlogtostderr) ||
                            (is_stdout && FLAGS_colorlogtostdout);
    const ColorMode mode =
        want_color ? StreamColorMode(output) : ColorMode::kNone;
    if (mode == ColorMode::kNone) {
      fwrite(data.message_text_, data.num_chars_to_log_, 1, output);
      MaybeFlushStdout(output, data.severity_);
      return;
    }

    const Theme& theme = DefaultTheme();
    const ColorSpec text_spec = theme.Get(SeverityToRole(data.severity_));
    FileFormatter formatter{output};
    const bool hyperlinks_enabled = mode == ColorMode::kAnsi &&
                                    FLAGS_symbolize_hyperlinks &&
                                    StreamSupportsHyperlinks(output);
    if (data.styles_.size() == 0) {
      WriteStyledLogField(output, formatter, mode,
                          TextAttributes{text_spec, Hyperlink()}, false,
                          data.message_text_, data.num_chars_to_log_);
    } else {
      WriteStyledLogBody(output, formatter, mode, data.styles_, text_spec,
                         hyperlinks_enabled, data.message_text_, 0,
                         data.num_chars_to_log_);
    }
    MaybeFlushStdout(output, data.severity_);
    return;
  }

  const bool is_stdout = (output == stdout);
  const bool want_color = (!is_stdout && FLAGS_colorlogtostderr) ||
                          (is_stdout && FLAGS_colorlogtostdout);
  const char* const message = data.message_text_;
  const size_t len = data.num_chars_to_log_;
  const ColorMode mode =
      want_color ? StreamColorMode(output) : ColorMode::kNone;

  // Avoid using cerr from this module since we may get called during
  // exit code, and cerr may be partially or fully destroyed by then.
  if (mode == ColorMode::kNone) {
    fwrite(message, len, 1, output);
    MaybeFlushStdout(output, data.severity_);
    return;
  }

  const Theme& theme = DefaultTheme();
  const ColorSpec text_spec = theme.Get(SeverityToRole(data.severity_));
  const bool text_colored = text_spec.foreground != Color::kDefault ||
                            text_spec.background != Color::kDefault ||
                            text_spec.style != TextStyle::kNone;
  FileFormatter formatter{output};

  // The severity character, timestamp, thread id, file:line, and closing
  // "]" fields of the prefix are each colored separately. The message
  // body keeps the existing whole-block, severity-based coloring. The
  // severity character shares the message body's severity-based color,
  // and the timestamp shares the crash banner's "date -d" command color.
  size_t pos = 0;
  WriteStyledLogField(output, formatter, mode,
                      TextAttributes{text_spec, Hyperlink()}, false,
                      message + pos, data.prefix_severity_len_);
  pos += data.prefix_severity_len_;

  WriteStyledLogField(
      output, formatter, mode,
      TextAttributes{theme.Get(Role::kShellCommand), Hyperlink()}, false,
      message + pos, data.prefix_timestamp_len_);
  pos += data.prefix_timestamp_len_;
  fwrite(message + pos, 1, 1, output);  // The space before the thread id.
  pos += 1;

  WriteStyledLogField(
      output, formatter, mode,
      TextAttributes{theme.Get(Role::kLogThreadId), Hyperlink()}, false,
      message + pos, data.prefix_threadid_len_);
  pos += data.prefix_threadid_len_;
  fwrite(message + pos, 1, 1, output);  // The space before "file:line".
  pos += 1;

  char uri[1024];
  const char* uri_pointer = nullptr;

  if (mode == ColorMode::kAnsi && FLAGS_symbolize_hyperlinks &&
      StreamSupportsHyperlinks(output)) {
    char span[600];
    const int span_len =
        std::snprintf(span, sizeof(span), "%s:%d", data.fullname_, data.line_);
    if (span_len > 0 && static_cast<size_t>(span_len) < sizeof(span)) {
      if (BuildFileLineUri(span, static_cast<size_t>(span_len),
                           FLAGS_symbolize_file_base_path.c_str(),
                           CachedHostname().c_str(), uri, sizeof(uri))) {
        uri_pointer = uri;
      }
    }
  }
  const ColorSpec file_line_spec = theme.Get(Role::kLogFileLine);
  const char* const file_line_text = message + pos;
  const size_t file_line_len = data.prefix_fileline_len_;

  WriteStyledLogField(output, formatter, mode,
                      TextAttributes{file_line_spec, Hyperlink(uri_pointer)},
                      true, file_line_text, file_line_len);
  pos += data.prefix_fileline_len_;

  WriteStyledLogField(output, formatter, mode,
                      TextAttributes{theme.Get(Role::kLogBracket), Hyperlink()},
                      false, message + pos, 1);  // The "]" closing the prefix.
  pos += 1;
  fwrite(message + pos, 1, 1, output);  // The space after "]".
  pos += 1;

  if (!text_colored && data.styles_.size() == 0) {
    fwrite(message + pos, len - pos, 1, output);
    MaybeFlushStdout(output, data.severity_);
    return;
  }

  const bool hyperlinks_enabled = mode == ColorMode::kAnsi &&
                                  FLAGS_symbolize_hyperlinks &&
                                  StreamSupportsHyperlinks(output);
  WriteStyledLogBody(output, formatter, mode, data.styles_, text_spec,
                     hyperlinks_enabled, message, pos, len);
  MaybeFlushStdout(output, data.severity_);
}

static void ColoredWriteToStdoutWithFields(
    const internal::LogMessageData& data) {
  FILE* output = stdout;
  // We also need to send logs to the stderr when the severity is
  // higher or equal to the stderr threshold.
  if (data.severity_ >= FLAGS_stderrthreshold) {
    output = stderr;
  }
  ColoredWriteToStderrOrStdoutWithFields(output, data);
}

static void ColoredWriteToStderrWithFields(
    const internal::LogMessageData& data) {
  ColoredWriteToStderrOrStdoutWithFields(stderr, data);
}

static void WriteToStderr(const char* message, size_t len) {
  // Avoid using cerr from this module since we may get called during
  // exit code, and cerr may be partially or fully destroyed by then.
  fwrite(message, len, 1, stderr);
}

static void WritePreInitializationWarning() {
  constexpr char kWarning[] = "WARNING";
  constexpr char kPrefix[] = ": Logging before ";
  constexpr char kSymbol[] = "InitializeLogging()";
  constexpr char kMiddle[] = " is written to ";
  constexpr char kStream[] = "STDERR";
  constexpr char kNewline[] = "\n";

  const ColorMode mode =
      FLAGS_colorlogtostderr ? StreamColorMode(stderr) : ColorMode::kNone;
  const Theme& theme = DefaultTheme();
  const auto write_content = [](const char* text, size_t) {
    WriteRawToStderr(text);
  };
  const auto write_styled = [mode, &write_content](ColorSpec spec,
                                                   const char* text) {
    WriteStyledField(spec, mode, write_content, text, strlen(text));
  };
  const auto write_plain = [](const char* text) { WriteRawToStderr(text); };

  write_styled(theme.Get(Role::kLogWarning), kWarning);
  write_plain(kPrefix);
  write_styled(theme.Get(Role::kStackFunction), kSymbol);
  write_plain(kMiddle);
  write_styled(theme.Get(Role::kMetaIdentifier), kStream);
  write_plain(kNewline);
}

inline void LogDestination::MaybeLogToStderr(
    const internal::LogMessageData& data) {
  if ((data.severity_ >= FLAGS_stderrthreshold) || FLAGS_alsologtostderr) {
    ColoredWriteToStderrWithFields(data);
    AlsoErrorWrite(data.severity_, tools::ProgramInvocationShortName(),
                   data.message_text_ + data.num_prefix_chars_);
  }
}

inline void LogDestination::MaybeLogToEmail(LogSeverity severity,
                                            const char* message, size_t len) {
  if (severity >= email_logging_severity_ || severity >= FLAGS_logemaillevel) {
    string to(FLAGS_alsologtoemail);
    if (!addresses_.empty()) {
      if (!to.empty()) {
        to += ",";
      }
      to += addresses_;
    }
    const string subject(string("[LOG] ") + LogSeverityNames[severity] + ": " +
                         tools::ProgramInvocationShortName());
    string body(hostname());
    body += "\n\n";
    body.append(message, len);

    // should NOT use SendEmail().  The caller of this function holds the
    // log_mutex and SendEmail() calls LOG/VLOG which will block trying to
    // acquire the log_mutex object.  Use SendEmailInternal() and set
    // use_logging to false.
    SendEmailInternal(to.c_str(), subject.c_str(), body.c_str(), false);
  }
}

inline void LogDestination::MaybeLogToLogfile(
    LogSeverity severity,
    const std::chrono::system_clock::time_point& timestamp, const char* message,
    size_t len) {
  const bool should_flush = severity > FLAGS_logbuflevel;
  LogDestination* destination = log_destination(severity);
  destination->logger_->Write(should_flush, timestamp, message, len);
}

inline void LogDestination::MaybeLogToLogfileLocked(
    LogSeverity severity,
    const std::chrono::system_clock::time_point& timestamp, const char* message,
    size_t len) {
  const bool should_flush = severity > FLAGS_logbuflevel;
  LogDestination* destination = log_destination(severity);
  if (destination->logger_ == &destination->fileobject_) {
    destination->fileobject_.WriteUnlocked(should_flush, timestamp, message,
                                           len, nullptr);
  } else {
    destination->logger_->Write(should_flush, timestamp, message, len);
  }
}

inline void LogDestination::LogToAllLogfiles(
    LogSeverity severity,
    const std::chrono::system_clock::time_point& timestamp, const char* message,
    size_t len) {
  if (FLAGS_logtostdout) {  // global flag: never log to file
    ColoredWriteToStdout(severity, message, len);
  } else if (FLAGS_logtostderr) {  // global flag: never log to file
    ColoredWriteToStderr(severity, message, len);
  } else {
    for (int i = severity; i >= 0; --i) {
      LogDestination::MaybeLogToLogfile(static_cast<LogSeverity>(i), timestamp,
                                        message, len);
    }
  }
}

inline void LogDestination::LogToAllLogfilesLocked(
    LogSeverity severity,
    const std::chrono::system_clock::time_point& timestamp, const char* message,
    size_t len) {
  if (FLAGS_logtostdout) {
    ColoredWriteToStdout(severity, message, len);
  } else if (FLAGS_logtostderr) {
    ColoredWriteToStderr(severity, message, len);
  } else {
    for (int i = severity; i >= 0; --i) {
      LogDestination::MaybeLogToLogfileLocked(static_cast<LogSeverity>(i),
                                              timestamp, message, len);
    }
  }
}

inline bool LogDestination::LogToSinks(LogSeverity severity,
                                       const char* full_filename,
                                       const char* base_filename, int line,
                                       const LogMessageTime& time,
                                       const char* message,
                                       size_t message_len) {
  const auto sinks = GetSinksForCall();
  if (!sinks) {
    return false;
  }
  for (size_t i = sinks->size(); i-- > 0;) {
    LogSink* sink = (*sinks)[i];
    SinkCallGuard guard{sink};
    sink->send(severity, full_filename, base_filename, line, time, message,
               message_len);
    sink->WaitTillSent();
  }
  return true;
}

inline void LogDestination::WaitForSinks(internal::LogMessageData* data,
                                         bool wait_for_registered_sinks) {
  if (wait_for_registered_sinks) {
    const auto sinks = GetSinksForCall();
    if (sinks) {
      for (size_t i = sinks->size(); i-- > 0;) {
        LogSink* sink = (*sinks)[i];
        SinkCallGuard guard{sink};
        sink->WaitTillSent();
      }
    }
  }
  const bool send_to_sink =
      (data->send_method_ == &LogMessage::SendToSink) ||
      (data->send_method_ == &LogMessage::SendToSinkAndLog);
  if (send_to_sink && data->sink_ != nullptr) {
    data->sink_->WaitTillSent();
  }
}

std::shared_ptr<const std::vector<LogSink*>> LogDestination::GetSinksForCall() {
  if (!sinks_present_.load(std::memory_order_acquire)) {
    return nullptr;
  }

  std::shared_lock<SinkMutex> l{sink_mutex_};
  const auto sinks = sinks_;
  if (sinks) {
    std::lock_guard<std::mutex> call_lock{sink_call_mutex_};
    for (size_t i = sinks->size(); i-- > 0;) {
      LogSink* sink = (*sinks)[i];
      ++sink_call_counts_[sink];
      ++sink_call_counts_for_thread[sink];
    }
  }
  return sinks;
}

void LogDestination::EndSinkCall(LogSink* sink) {
  std::lock_guard<std::mutex> l{sink_call_mutex_};
  auto it = sink_call_counts_.find(sink);
  CHECK(it != sink_call_counts_.end());
  CHECK_GT(it->second, 0U);
  --it->second;
  auto thread_it = sink_call_counts_for_thread.find(sink);
  CHECK(thread_it != sink_call_counts_for_thread.end());
  CHECK_GT(thread_it->second, 0U);
  --thread_it->second;
  if (thread_it->second == 0) {
    sink_call_counts_for_thread.erase(thread_it);
  }
  if (it->second == 0) {
    sink_call_counts_.erase(it);
  }
  sink_call_cond_.notify_all();
}

void LogDestination::WaitForSinkCalls(LogSink* sink) {
  std::unique_lock<std::mutex> l{sink_call_mutex_};
  sink_call_cond_.wait(l, [sink] {
    const auto global_it = sink_call_counts_.find(sink);
    const auto thread_it = sink_call_counts_for_thread.find(sink);
    const size_t thread_count =
        thread_it == sink_call_counts_for_thread.end() ? 0 : thread_it->second;
    const size_t global_count =
        global_it == sink_call_counts_.end() ? 0 : global_it->second;
    return global_count == thread_count;
  });
}

void LogDestination::WaitForAllSinkCalls() {
  std::unique_lock<std::mutex> l{sink_call_mutex_};
  sink_call_cond_.wait(l, [] {
    for (const auto& entry : sink_call_counts_) {
      const auto thread_it = sink_call_counts_for_thread.find(entry.first);
      const size_t thread_count = thread_it == sink_call_counts_for_thread.end()
                                      ? 0
                                      : thread_it->second;
      if (entry.second != thread_count) {
        return false;
      }
    }
    for (const auto& entry : sink_call_counts_for_thread) {
      const auto global_it = sink_call_counts_.find(entry.first);
      if (global_it == sink_call_counts_.end() ||
          global_it->second != entry.second) {
        return false;
      }
    }
    return true;
  });
}

std::unique_ptr<LogDestination>
    LogDestination::log_destinations_[NUM_SEVERITIES];

inline LogDestination* LogDestination::log_destination(LogSeverity severity) {
  if (log_destinations_[severity] == nullptr) {
    log_destinations_[severity] =
        std::make_unique<LogDestination>(severity, nullptr);
  }
  return log_destinations_[severity].get();
}

void LogDestination::DeleteLogDestinations() {
  for (auto& log_destination : log_destinations_) {
    log_destination.reset();
  }
  {
    SinkLock l{sink_mutex_};
    sinks_.reset();
    sinks_present_.store(false, std::memory_order_release);
  }
  WaitForAllSinkCalls();
}

namespace {

std::string g_application_fingerprint;

}  // namespace

void SetApplicationFingerprint(const std::string& fingerprint) {
  g_application_fingerprint = fingerprint;
}

namespace {

// Directory delimiter; Windows supports both forward slashes and backslashes
#ifdef NGLOG_OS_WINDOWS
const char possible_dir_delim[] = {'\\', '/'};
#else
const char possible_dir_delim[] = {'/'};
#endif

string PrettyDuration(const std::chrono::duration<int>& secs) {
  std::stringstream result;
  int mins = secs.count() / 60;
  int hours = mins / 60;
  mins = mins % 60;
  int s = secs.count() % 60;
  result.fill('0');
  result << hours << ':' << setw(2) << mins << ':' << setw(2) << s;
  return result.str();
}

LogFileObject::LogFileObject(LogSeverity severity, const char* base_filename)
    : base_filename_selected_(base_filename != nullptr),
      base_filename_((base_filename != nullptr) ? base_filename : ""),
      symlink_basename_(tools::ProgramInvocationShortName()),
      filename_extension_(),
      severity_(severity),
      rollover_attempt_(kRolloverAttemptFrequency - 1),
      start_time_(std::chrono::system_clock::now()) {}

LogFileObject::~LogFileObject() {
  std::lock_guard<internal::FileMutex> l{mutex_};
  file_ = nullptr;
}

void LogFileObject::SetBasename(const char* basename) {
  std::lock_guard<internal::FileMutex> l{mutex_};
  base_filename_selected_ = true;
  if (base_filename_ != basename) {
    // Get rid of old log file since we are changing names
    if (file_ != nullptr) {
      file_ = nullptr;
    }
    file_length_ = bytes_since_flush_ = dropped_mem_length_ = 0;
    rollover_attempt_ = kRolloverAttemptFrequency - 1;
    base_filename_ = basename;
  }
}

void LogFileObject::SetExtension(const char* ext) {
  std::lock_guard<internal::FileMutex> l{mutex_};
  if (filename_extension_ != ext) {
    // Get rid of old log file since we are changing names
    if (file_ != nullptr) {
      file_ = nullptr;
    }
    file_length_ = bytes_since_flush_ = dropped_mem_length_ = 0;
    rollover_attempt_ = kRolloverAttemptFrequency - 1;
    filename_extension_ = ext;
  }
}

void LogFileObject::SetSymlinkBasename(const char* symlink_basename) {
  std::lock_guard<internal::FileMutex> l{mutex_};
  symlink_basename_ = symlink_basename;
}

void LogFileObject::Flush() {
  std::lock_guard<internal::FileMutex> l{mutex_};
  FlushUnlocked(std::chrono::system_clock::now());
}

void LogFileObject::FlushUnlocked(
    const std::chrono::system_clock::time_point& now) {
  if (file_ != nullptr) {
    fflush(file_.get());
    bytes_since_flush_ = 0;
  }
  // Figure out when we are due for another flush.
  next_flush_time_ =
      now + std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::duration<int32>{FLAGS_logbufsecs});
}

bool LogFileObject::CreateLogfile(const string& time_pid_string) {
#ifdef NGLOG_OS_WINDOWS
  create_error_message_.clear();
#endif
  string string_filename = base_filename_;
  if (FLAGS_timestamp_in_logfile_name) {
    string_filename += time_pid_string;
  }
  string_filename += filename_extension_;
#ifdef NGLOG_OS_WINDOWS
  create_filename_ = string_filename;
#endif
  const char* filename = string_filename.c_str();
  // only write to files, create if non-existent.
  int flags = O_WRONLY | O_CREAT;
#if defined(NGLOG_OS_WINDOWS)
  bool truncate_file = false;
#endif
  if (FLAGS_timestamp_in_logfile_name) {
    // demand that the file is unique for our timestamp (fail if it exists).
    flags = flags | O_EXCL;
  } else {
    // logs are written to a single file, where: a log file is created for the
    // the first time or a file is being recreated due to exceeding max size

    struct stat statbuf;
    if (stat(filename, &statbuf) == 0) {
      // truncate the file if it exceeds the max size
      if ((static_cast<std::size_t>(statbuf.st_size) >> 20U) >= MaxLogSize()) {
#if defined(NGLOG_OS_WINDOWS)
        truncate_file = true;
#else
        flags |= O_TRUNC;
#endif
      }

      // update file length to sync file size
      file_length_ = static_cast<std::size_t>(statbuf.st_size);
    }
  }

  FileDescriptor fd{
      open(filename, flags, static_cast<mode_t>(FLAGS_logfile_mode))};
  if (!fd) {
#ifdef NGLOG_OS_WINDOWS
    create_error_message_ = StrError(errno);
#endif
    return false;
  }
  // Mark the file as exclusive write access to avoid two clients logging to
  // the same file. This applies particularly when
  // !FLAGS_timestamp_in_logfile_name, because timestamped files are opened
  // with O_EXCL.
  // Locks are released automatically when the file descriptor is closed.
#if defined(NGLOG_OS_WINDOWS)
  const HANDLE file_handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd.get()));
  if (file_handle == INVALID_HANDLE_VALUE) {
    create_error_message_ = StrError(errno);
    return false;
  }

  OVERLAPPED lock_offset = {};
  lock_offset.Offset = static_cast<DWORD>(kLogFileLockOffset);
  lock_offset.OffsetHigh =
      static_cast<DWORD>(kLogFileLockOffset >> kWindowsDwordBits);
  const DWORD lock_flags = LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY;
  if (!LockFileEx(file_handle, lock_flags, 0, kLogFileLockLength, 0,
                  &lock_offset)) {
    create_error_message_ = WindowsErrorMessage(GetLastError());
    return false;
  }
  if (truncate_file) {
    const LARGE_INTEGER file_offset = {};
    if (!SetFilePointerEx(file_handle, file_offset, nullptr, FILE_BEGIN) ||
        !SetEndOfFile(file_handle)) {
      create_error_message_ = WindowsErrorMessage(GetLastError());
      return false;
    }
    file_length_ = 0;
  }
#elif defined(HAVE_FCNTL)
  // Mark the file close-on-exec. We don't really care if this fails
  fcntl(fd.get(), F_SETFD, FD_CLOEXEC);
  // This will work after a fork as it is not inherited (not stored in the fd).
  // Lock will not be lost because the file is opened with exclusive lock
  // (write) and we will never read from it inside the process.
  struct flock w_lock = {};
  w_lock.l_type = F_WRLCK;
  w_lock.l_start = 0;
  w_lock.l_whence = SEEK_SET;
  w_lock.l_len = 0;

  int wlock_ret = fcntl(fd.get(), F_SETLK, &w_lock);
  if (wlock_ret == -1) {
    return false;
  }
#endif

  // fdopen in append mode so if the file exists it will fseek to the end
  file_.reset(fdopen(fd.release(), "a"));  // Make a FILE*.
  if (file_ == nullptr) {                  // Man, we're screwed!
#ifdef NGLOG_OS_WINDOWS
    create_error_message_ = StrError(errno);
#endif
    if (FLAGS_timestamp_in_logfile_name) {
      unlink(filename);  // Erase the half-baked evidence: an unusable log file,
                         // only if we just created it.
    }
    return false;
  }
#ifdef NGLOG_OS_WINDOWS
  // https://github.com/golang/go/issues/27638 - make sure we seek to the end to
  // append empirically replicated with wine over mingw build
  if (!FLAGS_timestamp_in_logfile_name) {
    if (fseek(file_.get(), 0, SEEK_END) != 0) {
#  ifdef NGLOG_OS_WINDOWS
      create_error_message_ = StrError(errno);
#  endif
      return false;
    }
  }
#endif
  // We try to create a symlink called <program_name>.<severity>,
  // which is easier to use.  (Every time we create a new logfile,
  // we destroy the old symlink and create a new one, so it always
  // points to the latest logfile.)  If it fails, we're sad but it's
  // no error.
  if (!symlink_basename_.empty()) {
    // take directory from filename
    const char* slash = strrchr(filename, PATH_SEPARATOR);
    const string linkname =
        symlink_basename_ + '.' + LogSeverityNames[severity_];
    string linkpath;
    if (slash)
      linkpath = string(
          filename, static_cast<size_t>(slash - filename + 1));  // get dirname
    linkpath += linkname;
    unlink(linkpath.c_str());  // delete old one if it exists

#if defined(NGLOG_OS_WINDOWS)
    // TODO(hamaji): Create lnk file on Windows?
#elif defined(HAVE_UNISTD_H)
    // We must have unistd.h.
    // Make the symlink be relative (in the same dir) so that if the
    // entire log directory gets relocated the link is still valid.
    const char* linkdest = slash ? (slash + 1) : filename;
    if (symlink(linkdest, linkpath.c_str()) != 0) {
      // silently ignore failures
    }

    // Make an additional link to the log file in a place specified by
    // FLAGS_log_link, if indicated
    if (!FLAGS_log_link.empty()) {
      linkpath = FLAGS_log_link + "/" + linkname;
      unlink(linkpath.c_str());  // delete old one if it exists
      if (symlink(filename, linkpath.c_str()) != 0) {
        // silently ignore failures
      }
    }
#endif
  }

  return true;  // Everything worked
}

void LogFileObject::Write(
    bool force_flush, const std::chrono::system_clock::time_point& timestamp,
    const char* message, size_t message_len) {
  std::unique_lock<internal::FileMutex> l{mutex_};
  WriteUnlocked(force_flush, timestamp, message, message_len, &l);
}

void LogFileObject::WriteUnlocked(
    bool force_flush, const std::chrono::system_clock::time_point& timestamp,
    const char* message, size_t message_len,
    std::unique_lock<internal::FileMutex>* lock) {
#if !defined(NGLOG_OS_LINUX) || !defined(HAVE_POSIX_FADVISE)
  (void)lock;
#endif

  // We don't log if the base_name_ is "" (which means "don't write")
  if (base_filename_selected_ && base_filename_.empty()) {
    return;
  }

  if (file_length_ >> 20U >= MaxLogSize() || PidHasChanged()) {
    file_ = nullptr;
    file_length_ = bytes_since_flush_ = dropped_mem_length_ = 0;
    rollover_attempt_ = kRolloverAttemptFrequency - 1;
  }

  // If there's no destination file, make one before outputting
  if (file_ == nullptr) {
    // Try to rollover the log file every 32 log messages.  The only time
    // this could matter would be when we have trouble creating the log
    // file.  If that happens, we'll lose lots of log messages, of course!
    if (++rollover_attempt_ != kRolloverAttemptFrequency) return;
    rollover_attempt_ = 0;

    struct ::tm tm_time;
    std::time_t t = std::chrono::system_clock::to_time_t(timestamp);

    if (FLAGS_log_utc_time) {
      gmtime_r(&t, &tm_time);
    } else {
      localtime_r(&t, &tm_time);
    }

    // The logfile's filename will have the date/time & pid in it
    ostringstream time_pid_stream;
    time_pid_stream.fill('0');
    time_pid_stream << 1900 + tm_time.tm_year << setw(2) << 1 + tm_time.tm_mon
                    << setw(2) << tm_time.tm_mday << '-' << setw(2)
                    << tm_time.tm_hour << setw(2) << tm_time.tm_min << setw(2)
                    << tm_time.tm_sec << '.' << GetMainThreadPid();
    const string& time_pid_string = time_pid_stream.str();

    if (base_filename_selected_) {
      if (!CreateLogfile(time_pid_string)) {
#ifdef NGLOG_OS_WINDOWS
        const std::string& error_message = create_error_message_;
        WriteLogFileCreationError(create_filename_, error_message);
#else
        const int error_number = errno;
        fprintf(stderr, "Could not create log file '%s': %s\n",
                time_pid_string.c_str(), StrError(error_number).c_str());
#endif
        return;
      }
    } else {
      // If no base filename for logs of this severity has been set, use a
      // default base filename of
      // "<program name>.<hostname>.<user name>.log.<severity level>.".  So
      // logfiles will have names like
      // webserver.examplehost.root.log.INFO.19990817-150000.4354, where
      // 19990817 is a date (1999 August 17), 150000 is a time (15:00:00),
      // and 4354 is the pid of the logging process.  The date & time reflect
      // when the file was created for output.
      //
      // Where does the file get put?  Successively try the directories
      // "/tmp", and "."
      string stripped_filename(tools::ProgramInvocationShortName());
      string hostname;
      GetHostName(&hostname);

      string uidname = MyUserName();
      // We should not call CHECK() here because this function can be
      // called after holding on to log_mutex. We don't want to
      // attempt to hold on to the same mutex, and get into a
      // deadlock. Simply use a name like invalid-user.
      if (uidname.empty()) uidname = "invalid-user";

      stripped_filename = stripped_filename + '.' + hostname + '.' + uidname +
                          ".log." + LogSeverityNames[severity_] + '.';
      // We're going to (potentially) try to put logs in several different dirs
      const vector<string>& log_dirs = GetLoggingDirectories();

      // Go through the list of dirs, and try to create the log file in each
      // until we succeed or run out of options
      bool success = false;
      for (const auto& log_dir : log_dirs) {
        base_filename_ = log_dir + "/" + stripped_filename;
        if (CreateLogfile(time_pid_string)) {
          success = true;
          break;
        }
      }
      // If we never succeeded, we have to give up
      if (success == false) {
#ifdef NGLOG_OS_WINDOWS
        const std::string& error_message = create_error_message_;
        WriteLogFileCreationError(create_filename_, error_message);
#else
        const int error_number = errno;
        fprintf(stderr, "Could not create log file '%s': %s\n",
                time_pid_string.c_str(), StrError(error_number).c_str());
#endif
        return;
      }
    }

    // Let the log cleaner know about the naming pattern of the new file so
    // that logs matching it are removed once they become overdue.
    internal::g_log_cleaner.AddLogFilePattern(
        base_filename_selected_, base_filename_, filename_extension_);

    // Write a header message into the log file
    if (FLAGS_log_file_header) {
      ostringstream file_header_stream;
      file_header_stream.fill('0');
      file_header_stream << "Log file created at: " << 1900 + tm_time.tm_year
                         << '/' << setw(2) << 1 + tm_time.tm_mon << '/'
                         << setw(2) << tm_time.tm_mday << ' ' << setw(2)
                         << tm_time.tm_hour << ':' << setw(2) << tm_time.tm_min
                         << ':' << setw(2) << tm_time.tm_sec
                         << (FLAGS_log_utc_time ? " UTC\n" : "\n")
                         << "Running on machine: " << LogDestination::hostname()
                         << '\n';

      if (!g_application_fingerprint.empty()) {
        file_header_stream << "Application fingerprint: "
                           << g_application_fingerprint << '\n';
      }
      file_header_stream
          << "Running duration (h:mm:ss): "
          << PrettyDuration(
                 std::chrono::duration_cast<std::chrono::duration<int>>(
                     timestamp - start_time_))
          << '\n';
      // The hardcoded format line only describes the default prefix; with a
      // user-installed prefix formatter the actual line format is unknown
      // here, so advertising the default one would be wrong.
      if (g_prefix_formatter == nullptr) {
        const char* const date_time_format = FLAGS_log_year_in_prefix
                                                 ? "yyyymmdd hh:mm:ss.uuuuuu"
                                                 : "mmdd hh:mm:ss.uuuuuu";
        file_header_stream << "Log line format: [IWEF]" << date_time_format
                           << " threadid file:line] msg" << '\n';
      }
      const string& file_header_string = file_header_stream.str();

      const size_t header_len = file_header_string.size();
      fwrite(file_header_string.data(), 1, header_len, file_.get());
      file_length_ += header_len;
      bytes_since_flush_ += header_len;
    }
  }

  // Write to LOG file
  if (!stop_writing.load(std::memory_order_relaxed)) {
    // fwrite() doesn't return an error when the disk is full, for
    // messages that are less than 4096 bytes. When the disk is full,
    // it returns the message length for messages that are less than
    // 4096 bytes. fwrite() returns 4096 for message lengths that are
    // greater than 4096, thereby indicating an error.
    errno = 0;
    fwrite(message, 1, message_len, file_.get());
    if (FLAGS_stop_logging_if_full_disk &&
        errno == ENOSPC) {  // disk full, stop writing to disk
      stop_writing.store(true, std::memory_order_relaxed);  // until the disk is
      return;
    } else {
      file_length_ += message_len;
      bytes_since_flush_ += message_len;
    }
  } else {
    if (timestamp >= next_flush_time_) {
      stop_writing.store(false, std::memory_order_relaxed);
    }
    return;  // no need to flush
  }

  // See important msgs *now*.  Also, flush logs at least every 10^6 chars,
  // or every "FLAGS_logbufsecs" seconds.
  if (force_flush || (bytes_since_flush_ >= 1000000) ||
      (timestamp >= next_flush_time_)) {
    FlushUnlocked(timestamp);
#ifdef NGLOG_OS_LINUX
    // Only consider files >= 3MiB
    if (FLAGS_drop_log_memory && file_length_ >= (3U << 20U)) {
      // Don't evict the most recent 1-2MiB so as not to impact a tailer
      // of the log file and to avoid page rounding issue on linux < 4.7
      std::size_t total_drop_length =
          (file_length_ & ~((1U << 20U) - 1U)) - (1U << 20U);
      std::size_t this_drop_length = total_drop_length - dropped_mem_length_;
      if (this_drop_length >= (2U << 20U)) {
        // Only advise when >= 2MiB to drop
#  if defined(HAVE_POSIX_FADVISE)
        const off_t offset = static_cast<off_t>(dropped_mem_length_);
        const off_t length = static_cast<off_t>(this_drop_length);

        // Advance the bookkeeping before releasing the lock so that
        // concurrent writers do not recompute overlapping drop ranges and
        // a rollover while the lock is released keeps its freshly reset
        // accounting.
        dropped_mem_length_ = total_drop_length;

        // posix_fadvise() can block for a substantial amount of time (e.g.,
        // due to kernel-internal LRU draining or writeback under I/O
        // pressure). Duplicate the descriptor and release mutex_ while the
        // syscall runs so that other threads logging to this file are not
        // serialized behind it. The duplicate keeps the underlying open
        // file description alive even if this file gets rolled over while
        // the lock is released and preserves the close-on-exec flag set on
        // the original descriptor.
        FileDescriptor fd{fcntl(fileno(file_.get()), F_DUPFD_CLOEXEC, 0)};
        if (fd) {
          if (lock != nullptr) {
            lock->unlock();
          }
          posix_fadvise(fd.get(), offset, length, POSIX_FADV_DONTNEED);
          if (lock != nullptr) {
            lock->lock();
          }
        } else {
          // Duplicating the descriptor failed. Fall back to advising while
          // holding the lock.
          posix_fadvise(fileno(file_.get()), offset, length,
                        POSIX_FADV_DONTNEED);
        }
#  else
        dropped_mem_length_ = total_drop_length;
#  endif
      }
    }
#endif
  }
}

}  // namespace

// Static log data space to avoid alloc failures in a LOG(FATAL)
//
// Since multiple threads may call LOG(FATAL), and we want to preserve
// the data from the first call, we allocate one exclusive set of space
// and one thread-local set for each concurrent caller.
static internal::FatalMutex fatal_msg_lock;
static internal::CrashReason crash_reason;
static bool fatal_msg_exclusive = true;
static internal::LogMessageData fatal_msg_data_exclusive;
static thread_local internal::LogMessageData fatal_msg_data_shared;

// Static thread-local log data space to use, because typically at most one
// LogMessageData object exists (in this case glog makes zero heap memory
// allocations).
static thread_local bool thread_data_available = true;

#if defined(__cpp_lib_byte) && __cpp_lib_byte >= 201603L
// std::aligned_storage is deprecated in C++23
alignas(internal::LogMessageData) static thread_local std::byte
    thread_msg_data[sizeof(internal::LogMessageData)];
#else   // !(defined(__cpp_lib_byte) && __cpp_lib_byte >= 201603L)
static thread_local std::aligned_storage<
    sizeof(internal::LogMessageData), alignof(internal::LogMessageData)>::type
    thread_msg_data;
#endif  // defined(__cpp_lib_byte) && __cpp_lib_byte >= 201603L

internal::LogMessageData::LogMessageData()
    : stream_(message_text_, LogMessage::kMaxLogMessageLen, 0), styles_() {}

LogMessage::LogMessage(const char* file, int line, LogSeverity severity,
                       int64 ctr, void (LogMessage::*send_method)())
    : allocated_(nullptr) {
  Init(file, line, severity, send_method);
  data_->stream_.set_ctr(ctr);
}

LogMessage::LogMessage(const char* file, int line,
                       const internal::CheckOpString& result)
    : allocated_(nullptr) {
  Init(file, line, NGLOG_FATAL, &LogMessage::SendToLog);
  stream() << "Check failed: " << (*result.str_) << " ";
}

LogMessage::LogMessage(const char* file, int line) : allocated_(nullptr) {
  Init(file, line, NGLOG_INFO, &LogMessage::SendToLog);
}

LogMessage::LogMessage(const char* file, int line, LogSeverity severity)
    : allocated_(nullptr) {
  Init(file, line, severity, &LogMessage::SendToLog);
}

LogMessage::LogMessage(const char* file, int line, LogSeverity severity,
                       LogSink* sink, bool also_send_to_log)
    : allocated_(nullptr) {
  Init(file, line, severity,
       also_send_to_log ? &LogMessage::SendToSinkAndLog
                        : &LogMessage::SendToSink);
  data_->sink_ = sink;  // override Init()'s setting to nullptr
}

LogMessage::LogMessage(const char* file, int line, LogSeverity severity,
                       vector<string>* outvec)
    : allocated_(nullptr) {
  Init(file, line, severity, &LogMessage::SaveOrSendToLog);
  data_->outvec_ = outvec;  // override Init()'s setting to nullptr
}

LogMessage::LogMessage(const char* file, int line, LogSeverity severity,
                       string* message)
    : allocated_(nullptr) {
  Init(file, line, severity, &LogMessage::WriteToStringAndLog);
  data_->message_ = message;  // override Init()'s setting to nullptr
}

void LogMessage::Init(const char* file, int line, LogSeverity severity,
                      void (LogMessage::*send_method)()) {
  allocated_ = nullptr;
  if (severity != NGLOG_FATAL ||
      !exit_on_dfatal.load(std::memory_order_relaxed)) {
    // No need for locking, because this is thread local.
    if (thread_data_available) {
      thread_data_available = false;
      data_ = new (&thread_msg_data) internal::LogMessageData;
    } else {
      allocated_ = new internal::LogMessageData();
      data_ = allocated_;
    }
    data_->first_fatal_ = false;
  } else {
    std::lock_guard<internal::FatalMutex> l{fatal_msg_lock};
    if (fatal_msg_exclusive) {
      fatal_msg_exclusive = false;
      data_ = &fatal_msg_data_exclusive;
      data_->first_fatal_ = true;
    } else {
      data_ = &fatal_msg_data_shared;
      data_->first_fatal_ = false;
    }
  }

  data_->preserved_errno_ = errno;
  data_->severity_ = severity;
  data_->line_ = line;
  data_->send_method_ = send_method;
  data_->sink_ = nullptr;
  data_->outvec_ = nullptr;

  const auto now = std::chrono::system_clock::now();
  time_ = LogMessageTime(now);

  data_->num_chars_to_log_ = 0;
  data_->num_chars_to_syslog_ = 0;
  data_->styles_.Reset();
  data_->stream_.SetStyleRecorder(&data_->styles_);
  data_->basename_ = const_basename(file);
  data_->fullname_ = file;
  data_->has_been_flushed_ = false;
  data_->thread_id_ = std::this_thread::get_id();

  data_->has_default_prefix_ = false;

  // If specified, prepend a prefix to each line.  For example:
  //    I20201018 160715 f5d4fbb0 logging.cc:1153]
  //    (log level, GMT year, month, date, time, thread_id, file basename, line)
  // We exclude the thread_id for the default thread.
  if (FLAGS_log_prefix && (line != kNoLogPrefix)) {
    if (g_prefix_formatter == nullptr) {
      AppendDefaultPrefix(*data_, severity, time_);
    } else {
      std::ios saved_fmt(nullptr);
      saved_fmt.copyfmt(stream());
      stream().fill('0');
      (*g_prefix_formatter)(stream(), *this);
      stream() << " ";
      stream().copyfmt(saved_fmt);
    }
  }
  data_->num_prefix_chars_ = data_->stream_.pcount();

#ifdef HAVE_STACKTRACE
  if (!FLAGS_log_backtrace_at.empty()) {
    char fileline[128];
    const int fileline_len = std::snprintf(fileline, sizeof(fileline), "%s:%d",
                                           data_->basename_, line);
    if (fileline_len >= 0 &&
        static_cast<std::size_t>(fileline_len) < sizeof(fileline) &&
        FLAGS_log_backtrace_at == fileline) {
      string stacktrace = GetStackTrace();
      stream() << " (stacktrace:\n" << stacktrace << ") ";
    }
  }
#endif
}

LogSeverity LogMessage::severity() const noexcept { return data_->severity_; }

int LogMessage::line() const noexcept { return data_->line_; }
const std::thread::id& LogMessage::thread_id() const noexcept {
  return data_->thread_id_;
}
const char* LogMessage::fullname() const noexcept { return data_->fullname_; }
const char* LogMessage::basename() const noexcept { return data_->basename_; }
const LogMessageTime& LogMessage::time() const noexcept { return time_; }

LogMessage::~LogMessage() noexcept(false) {
  Flush();
  const bool fail = data_->severity_ == NGLOG_FATAL &&
                    exit_on_dfatal.load(std::memory_order_relaxed);
  if (data_ == static_cast<void*>(&thread_msg_data)) {
    data_->~LogMessageData();
    thread_data_available = true;
  } else {
    delete allocated_;
  }

  if (fail) {
    const char* message = "*** Check failure stack trace: ***\n";
    if (write(fileno(stderr), message, strlen(message)) < 0) {
      // Ignore errors.
    }
    AlsoErrorWrite(NGLOG_FATAL, tools::ProgramInvocationShortName(), message);
#if defined(__cpp_lib_uncaught_exceptions) && \
    (__cpp_lib_uncaught_exceptions >= 201411L)
    if (std::uncaught_exceptions() == 0)
#else
    if (!std::uncaught_exception())
#endif
    {
      Fail();
    }
  }
}

int LogMessage::preserved_errno() const { return data_->preserved_errno_; }

ostream& LogMessage::stream() { return data_->stream_; }

void LogMessage::LogStream::SetStyleRecorder(
    internal::StyleRecorder* recorder) {
  style_recorder_ = recorder;
}

void LogMessage::LogStream::PushStyle(const TextAttributes& attributes) {
  if (style_recorder_ != nullptr) {
    style_recorder_->Push(pcount(), attributes);
  }
}

void LogMessage::LogStream::PopStyle() {
  if (style_recorder_ != nullptr) {
    style_recorder_->Pop(pcount());
  }
}

void LogMessage::AppendText(const char* text, std::size_t length,
                            TextAttributes attributes) {
  if (text == nullptr || length == 0) {
    return;
  }
  data_->stream_.PushStyle(attributes);
  data_->stream_.write(text, static_cast<std::streamsize>(length));
  data_->stream_.PopStyle();
}

// Flush buffered message, called by the destructor, or any other function
// that needs to synchronize the log.
void LogMessage::Flush() {
  if (data_->has_been_flushed_ || data_->severity_ < FLAGS_minloglevel) {
    return;
  }

  data_->num_chars_to_log_ = data_->stream_.pcount();
  data_->num_chars_to_syslog_ =
      data_->num_chars_to_log_ - data_->num_prefix_chars_;
  data_->styles_.Close(data_->num_chars_to_log_);

  // Do we need to add a \n to the end of this message?
  bool append_newline =
      (data_->message_text_[data_->num_chars_to_log_ - 1] != '\n');
  char original_final_char = '\0';

  // If we do need to add a \n, we'll do it by violating the memory of the
  // ostrstream buffer.  This is quick, and we'll make sure to undo our
  // modification before anything else is done with the ostrstream.  It
  // would be preferable not to do things this way, but it seems to be
  // the best way to deal with this.
  if (append_newline) {
    original_final_char = data_->message_text_[data_->num_chars_to_log_];
    data_->message_text_[data_->num_chars_to_log_++] = '\n';
  }
  data_->message_text_[data_->num_chars_to_log_] = '\0';

  const auto send_method = data_->send_method_;
  const bool send_to_sink = send_method == &LogMessage::SendToSink ||
                            send_method == &LogMessage::SendToSinkAndLog;
  const bool send_to_registered_sinks =
      send_method == &LogMessage::SendToLog ||
      send_method == &LogMessage::SendToSyslogAndLog ||
      send_method == &LogMessage::SendToSinkAndLog ||
      send_method == &LogMessage::WriteToStringAndLog ||
      (send_method == &LogMessage::SaveOrSendToLog &&
       data_->outvec_ == nullptr);
  const bool wait_for_sinks_before_count =
      send_to_registered_sinks && data_->severity_ == NGLOG_FATAL &&
      exit_on_dfatal.load(std::memory_order_relaxed);

  if (send_to_sink) {
    SendToSink();
  }

  if (send_method != &LogMessage::SendToSink) {
    // Protect the shared logging destinations while dispatching the message.
    std::lock_guard<internal::LogMutex> l{log_mutex};
    if (send_method == &LogMessage::SendToSinkAndLog) {
      SendToLog();
    } else {
      (this->*send_method)();
    }
  }

  bool registered_sinks_waited = false;
  if (send_to_registered_sinks) {
    registered_sinks_waited = SendToRegisteredSinks();
  }

  if (wait_for_sinks_before_count) {
    LogDestination::WaitForSinks(data_, !registered_sinks_waited);
  }
  ++num_messages_[static_cast<int>(data_->severity_)];
  if (!wait_for_sinks_before_count) {
    LogDestination::WaitForSinks(data_, !registered_sinks_waited);
  }

  if (append_newline) {
    // Fix the ostrstream back how it was before we screwed with it.
    // It's 99.44% certain that we don't need to worry about doing this.
    data_->message_text_[data_->num_chars_to_log_ - 1] = original_final_char;
  }

  // If errno was already set before we enter the logging call, we'll
  // set it back to that value when we return from the logging call.
  // It happens often that we log an error message after a syscall
  // failure, which can potentially set the errno to some other
  // values.  We would like to preserve the original errno.
  if (data_->preserved_errno_ != 0) {
    errno = data_->preserved_errno_;
  }

  // Note that this message is now safely logged.  If we're asked to flush
  // again, as a result of destruction, say, we'll do nothing on future calls.
  data_->has_been_flushed_ = true;
}

// Copy of first FATAL log message so that we can print it out again
// after all the stack traces.  To preserve legacy behavior, we don't
// use fatal_msg_data_exclusive.
static std::chrono::system_clock::time_point fatal_time;
static char fatal_message[256];

void ReprintFatalMessage() {
  if (fatal_message[0]) {
    const size_t n = strlen(fatal_message);
    if (!FLAGS_logtostderr) {
      // Also write to stderr (don't color to avoid terminal checks)
      WriteToStderr(fatal_message, n);
    }
    LogDestination::LogToAllLogfiles(NGLOG_ERROR, fatal_time, fatal_message, n);
  }
}

// L >= log_mutex (callers must hold the log_mutex).
void LogMessage::SendToLog() NGLOG_LOCKS_REQUIRED(log_mutex) {
  static bool already_warned_before_init = false;

  RAW_DCHECK(data_->num_chars_to_log_ > 0 &&
                 data_->message_text_[data_->num_chars_to_log_ - 1] == '\n',
             "");

  // Messages of a given severity get logged to lower severity logs, too

  if (!already_warned_before_init && !IsLoggingInitialized()) {
    WritePreInitializationWarning();
    already_warned_before_init = true;
  }

  // global flag: never log to file if set.  Also -- don't log to a
  // file if we haven't parsed the command line flags to get the
  // program name.
  if (FLAGS_logtostderr || FLAGS_logtostdout || !IsLoggingInitialized()) {
    if (FLAGS_logtostdout) {
      ColoredWriteToStdoutWithFields(*data_);
    } else {
      ColoredWriteToStderrWithFields(*data_);
    }

  } else {
    // log this message to all log files of severity <= severity_
    LogDestination::LogToAllLogfilesLocked(data_->severity_, time_.when(),
                                           data_->message_text_,
                                           data_->num_chars_to_log_);

    LogDestination::MaybeLogToStderr(*data_);
    LogDestination::MaybeLogToEmail(data_->severity_, data_->message_text_,
                                    data_->num_chars_to_log_);
    // NOTE: -1 removes trailing \n
  }

  // If we log a FATAL message, flush all the log destinations, then toss
  // a signal for others to catch. We leave the logs in a state that
  // someone else can use them (as long as they flush afterwards)
  if (data_->severity_ == NGLOG_FATAL &&
      exit_on_dfatal.load(std::memory_order_relaxed)) {
    if (data_->first_fatal_) {
      // Store crash information so that it is accessible from within signal
      // handlers that may be invoked later.
      RecordCrashReason(&crash_reason);
      SetCrashReason(&crash_reason);

      // Store shortened fatal message for other logs and GWQ status
      const size_t copy =
          min(data_->num_chars_to_log_, sizeof(fatal_message) - 1);
      memcpy(fatal_message, data_->message_text_, copy);
      fatal_message[copy] = '\0';
      fatal_time = time_.when();
    }

    if (!FLAGS_logtostderr && !FLAGS_logtostdout) {
      for (auto& log_destination : LogDestination::log_destinations_) {
        if (log_destination) {
          log_destination->logger_->Write(
              true, std::chrono::system_clock::time_point{}, "", 0);
        }
      }
    }
  }
}

void LogMessage::RecordCrashReason(internal::CrashReason* reason) {
  reason->filename = fatal_msg_data_exclusive.fullname_;
  reason->line_number = fatal_msg_data_exclusive.line_;
  reason->message = fatal_msg_data_exclusive.message_text_ +
                    fatal_msg_data_exclusive.num_prefix_chars_;
#ifdef HAVE_STACKTRACE
  // Retrieve the stack trace, omitting the logging frames that got us here.
  reason->depth = GetStackTrace(reason->stack, ARRAYSIZE(reason->stack), 4);
#else
  reason->depth = 0;
#endif
}

NGLOG_NO_EXPORT logging_fail_func_t g_logging_fail_func =
    reinterpret_cast<logging_fail_func_t>(&abort);

NullStream::NullStream() : LogMessage::LogStream(message_buffer_, 2, 0) {}
NullStream::NullStream(const char* /*file*/, int /*line*/,
                       const internal::CheckOpString& /*result*/)
    : LogMessage::LogStream(message_buffer_, 2, 0) {}
NullStream& NullStream::stream() { return *this; }

NullStreamFatal::~NullStreamFatal() {
  // Cannot use g_logging_fail_func here as it may output the backtrace which
  // would be inconsistent with NullStream behavior.
  std::abort();
}

logging_fail_func_t InstallFailureFunction(logging_fail_func_t fail_func) {
  return std::exchange(g_logging_fail_func, fail_func);
}

void LogMessage::Fail() {
  g_logging_fail_func();
  std::abort();
}

void LogMessage::SendToSink() {
  if (data_->sink_ != nullptr) {
    RAW_DCHECK(data_->num_chars_to_log_ > 0 &&
                   data_->message_text_[data_->num_chars_to_log_ - 1] == '\n',
               "");
    data_->sink_->send(
        data_->severity_, data_->fullname_, data_->basename_, data_->line_,
        time_, data_->message_text_ + data_->num_prefix_chars_,
        (data_->num_chars_to_log_ - data_->num_prefix_chars_ - 1));
  }
}

bool LogMessage::SendToRegisteredSinks() {
  return LogDestination::LogToSinks(
      data_->severity_, data_->fullname_, data_->basename_, data_->line_, time_,
      data_->message_text_ + data_->num_prefix_chars_,
      (data_->num_chars_to_log_ - data_->num_prefix_chars_ - 1));
}

// L >= log_mutex (callers must hold the log_mutex).
void LogMessage::SendToSinkAndLog() NGLOG_LOCKS_REQUIRED(log_mutex) {
  SendToSink();
  SendToLog();
}

// L >= log_mutex (callers must hold the log_mutex).
void LogMessage::SaveOrSendToLog() NGLOG_LOCKS_REQUIRED(log_mutex) {
  if (data_->outvec_ != nullptr) {
    RAW_DCHECK(data_->num_chars_to_log_ > 0 &&
                   data_->message_text_[data_->num_chars_to_log_ - 1] == '\n',
               "");
    // Omit prefix of message and trailing newline when recording in outvec_.
    const char* start = data_->message_text_ + data_->num_prefix_chars_;
    size_t len = data_->num_chars_to_log_ - data_->num_prefix_chars_ - 1;
    data_->outvec_->push_back(string(start, len));
  } else {
    SendToLog();
  }
}

void LogMessage::WriteToStringAndLog() NGLOG_LOCKS_REQUIRED(log_mutex) {
  if (data_->message_ != nullptr) {
    RAW_DCHECK(data_->num_chars_to_log_ > 0 &&
                   data_->message_text_[data_->num_chars_to_log_ - 1] == '\n',
               "");
    // Omit prefix of message and trailing newline when writing to message_.
    const char* start = data_->message_text_ + data_->num_prefix_chars_;
    size_t len = data_->num_chars_to_log_ - data_->num_prefix_chars_ - 1;
    data_->message_->assign(start, len);
  }
  SendToLog();
}

// L >= log_mutex (callers must hold the log_mutex).
void LogMessage::SendToSyslogAndLog() {
#ifdef HAVE_SYSLOG_H
  // Before any calls to syslog(), make a single call to openlog()
  static bool openlog_already_called = false;
  if (!openlog_already_called) {
    openlog(tools::ProgramInvocationShortName(),
            LOG_CONS | LOG_NDELAY | LOG_PID, LOG_USER);
    openlog_already_called = true;
  }

  // This array maps Google severity levels to syslog levels
  const int SEVERITY_TO_LEVEL[] = {LOG_INFO, LOG_WARNING, LOG_ERR, LOG_EMERG};
  syslog(LOG_USER | SEVERITY_TO_LEVEL[static_cast<int>(data_->severity_)],
         "%.*s", static_cast<int>(data_->num_chars_to_syslog_),
         data_->message_text_ + data_->num_prefix_chars_);
  SendToLog();
#else
  LOG(ERROR) << "No syslog support: message=" << data_->message_text_;
#endif
}

base::Logger* base::GetLogger(LogSeverity severity) {
  std::lock_guard<internal::LogMutex> l{log_mutex};
  return LogDestination::log_destination(severity)->GetLoggerImpl();
}

void base::SetLogger(LogSeverity severity, base::Logger* logger) {
  std::lock_guard<internal::LogMutex> l{log_mutex};
  LogDestination::log_destination(severity)->SetLoggerImpl(logger);
}

// L < log_mutex.
int64 LogMessage::num_messages(int severity) {
  return num_messages_[severity].load(std::memory_order_relaxed);
}

// Output the COUNTER value. This is only valid if ostream is a
// LogStream.
ostream& operator<<(ostream& os, const Counter_t&) {
#if !defined(__GXX_RTTI) && !defined(_CPPRTTI)
  LogMessage::LogStream* log = static_cast<LogMessage::LogStream*>(&os);
#else
  auto* log = dynamic_cast<LogMessage::LogStream*>(&os);
#endif
  CHECK(log && log == log->self())
      << "You must not use COUNTER with non-glog ostream";
  os << log->ctr();
  return os;
}

ErrnoLogMessage::ErrnoLogMessage(const char* file, int line,
                                 LogSeverity severity, int64 ctr,
                                 void (LogMessage::*send_method)())
    : LogMessage(file, line, severity, ctr, send_method) {}

ErrnoLogMessage::~ErrnoLogMessage() {
  // Don't access errno directly because it may have been altered
  // while streaming the message.
  const Theme& theme = DefaultTheme();
  stream() << PushStyle(theme.Get(Role::kErrnoMessage)) << ": "
           << StrError(preserved_errno()) << PopStyle()
           << PushStyle(theme.Get(Role::kErrnoCode)) << " ["
           << preserved_errno() << "]" << PopStyle();
}

void FlushLogFiles(LogSeverity min_severity) {
  LogDestination::FlushLogFiles(min_severity);
}

void FlushLogFilesUnsafe(LogSeverity min_severity) {
  LogDestination::FlushLogFilesUnsafe(min_severity);
}

void SetLogDestination(LogSeverity severity, const char* base_filename) {
  LogDestination::SetLogDestination(severity, base_filename);
}

void SetLogSymlink(LogSeverity severity, const char* symlink_basename) {
  LogDestination::SetLogSymlink(severity, symlink_basename);
}

LogSink::~LogSink() = default;

void LogSink::WaitTillSent() {
  // noop default
}

string LogSink::ToString(LogSeverity severity, const char* file, int line,
                         const LogMessageTime& time, const char* message,
                         size_t message_len) {
  ostringstream stream;
  stream.fill('0');

  stream << LogSeverityNames[severity][0];
  if (FLAGS_log_year_in_prefix) {
    stream << setw(4) << 1900 + time.year();
  }
  stream << setw(2) << 1 + time.month() << setw(2) << time.day() << ' '
         << setw(2) << time.hour() << ':' << setw(2) << time.min() << ':'
         << setw(2) << time.sec() << '.' << setw(6) << time.usec() << ' '
         << setfill(' ') << setw(5) << std::this_thread::get_id()
         << setfill('0') << ' ' << file << ':' << line << "] ";

  // A call to `write' is enclosed in parenthneses to prevent possible macro
  // expansion.  On Windows, `write' could be a macro defined for portability.
  (stream.write)(message, static_cast<std::streamsize>(message_len));
  return stream.str();
}

void AddLogSink(LogSink* destination) {
  LogDestination::AddLogSink(destination);
}

void RemoveLogSink(LogSink* destination) {
  LogDestination::RemoveLogSink(destination);
}

void SetLogFilenameExtension(const char* ext) {
  LogDestination::SetLogFilenameExtension(ext);
}

void SetStderrLogging(LogSeverity min_severity) {
  LogDestination::SetStderrLogging(min_severity);
}

void SetEmailLogging(LogSeverity min_severity, const char* addresses) {
  LogDestination::SetEmailLogging(min_severity, addresses);
}

void LogToStderr() { LogDestination::LogToStderr(); }

namespace base {
namespace internal {

bool GetExitOnDFatal();
bool GetExitOnDFatal() {
  std::lock_guard<::nglog::internal::LogMutex> l{log_mutex};
  return exit_on_dfatal.load(std::memory_order_relaxed);
}

// Determines whether we exit the program for a LOG(DFATAL) message in
// debug mode.  It does this by skipping the call to Fail/FailQuietly.
// This is intended for testing only.
//
// This can have some effects on LOG(FATAL) as well.  Failure messages
// are always allocated (rather than sharing a buffer), the crash
// reason is not recorded, the "gwq" status message is not updated,
// and the stack trace is not recorded.  The LOG(FATAL) *will* still
// exit the program.  Since this function is used only in testing,
// these differences are acceptable.
void SetExitOnDFatal(bool value);
void SetExitOnDFatal(bool value) {
  std::lock_guard<::nglog::internal::LogMutex> l{log_mutex};
  exit_on_dfatal.store(value, std::memory_order_relaxed);
}

}  // namespace internal
}  // namespace base

#ifndef NGLOG_OS_EMSCRIPTEN
// Shell-escaping as we need to shell out ot /bin/mail.
static const char kDontNeedShellEscapeChars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+-_.=/:,@";

static string ShellEscape(const string& src) {
  string result;
  if (!src.empty() &&  // empty string needs quotes
      src.find_first_not_of(kDontNeedShellEscapeChars) == string::npos) {
    // only contains chars that don't need quotes; it's fine
    result.assign(src);
  } else if (src.find_first_of('\'') == string::npos) {
    // no single quotes; just wrap it in single quotes
    result.assign("'");
    result.append(src);
    result.append("'");
  } else {
    // needs double quote escaping
    result.assign("\"");
    for (size_t i = 0; i < src.size(); ++i) {
      switch (src[i]) {
        case '\\':
        case '$':
        case '"':
        case '`':
          result.append("\\");
      }
      result.append(src, i, 1);
    }
    result.append("\"");
  }
  return result;
}

// Trim whitespace from both ends of the provided string.
static inline void trim(std::string& s) {
  const auto toRemove = [](char ch) { return std::isspace(ch) == 0; };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), toRemove));
  s.erase(std::find_if(s.rbegin(), s.rend(), toRemove).base(), s.end());
}
#endif

// use_logging controls whether the logging functions LOG/VLOG are used
// to log errors.  It should be set to false when the caller holds the
// log_mutex.
static bool SendEmailInternal(const char* dest, const char* subject,
                              const char* body, bool use_logging) {
#ifndef NGLOG_OS_EMSCRIPTEN
  if (dest && *dest) {
    // Split the comma-separated list of email addresses, validate each one and
    // build a sanitized new comma-separated string without whitespace.
    std::istringstream ss(dest);
    std::ostringstream sanitized_dests;
    std::string s;
    while (std::getline(ss, s, ',')) {
      trim(s);
      if (s.empty()) {
        continue;
      }
      // We validate the provided email addresses using the same regular
      // expression that HTML5 uses[1], except that we require the address to
      // start with an alpha-numeric character. This is because we don't want to
      // allow email addresses that start with a special character, such as a
      // pipe or dash, which could be misunderstood as a command-line flag by
      // certain versions of `mail` that are vulnerable to command injection.[2]
      // [1]
      // https://html.spec.whatwg.org/multipage/input.html#valid-e-mail-address
      // [2] e.g. https://nvd.nist.gov/vuln/detail/CVE-2004-2771
      if (!std::regex_match(
              s,
              std::regex("^[a-zA-Z0-9]"
                         "[a-zA-Z0-9.!#$%&'*+/=?^_`{|}~-]*@[a-zA-Z0-9]"
                         "(?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?(?:\\.[a-zA-Z0-9]"
                         "(?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*$"))) {
        if (use_logging) {
          VLOG(1) << "Invalid destination email address:" << s;
        } else {
          fprintf(stderr, "Invalid destination email address: %s\n", s.c_str());
        }
        return false;
      }
      if (!sanitized_dests.str().empty()) {
        sanitized_dests << ",";
      }
      sanitized_dests << s;
    }
    // Avoid dangling reference
    const std::string& tmp = sanitized_dests.str();
    dest = tmp.c_str();

    if (use_logging) {
      VLOG(1) << "Trying to send TITLE:" << subject << " BODY:" << body
              << " to " << dest;
    } else {
      fprintf(stderr, "Trying to send TITLE: %s BODY: %s to %s\n", subject,
              body, dest);
    }

    string logmailer;

    if (FLAGS_logmailer.empty()) {
      // Don't need to shell escape the literal string
      logmailer = "/bin/mail";
    } else {
      logmailer = ShellEscape(FLAGS_logmailer);
    }

    string cmd =
        logmailer + " -s" + ShellEscape(subject) + " " + ShellEscape(dest);
    if (use_logging) {
      VLOG(4) << "Mailing command: " << cmd;
    }

    FILE* pipe = popen(cmd.c_str(), "w");
    if (pipe != nullptr) {
      // Add the body if we have one
      if (body) {
        fwrite(body, sizeof(char), strlen(body), pipe);
      }
      bool ok = pclose(pipe) != -1;
      if (!ok) {
        if (use_logging) {
          LOG(ERROR) << "Problems sending mail to " << dest << ": "
                     << StrError(errno);
        } else {
          fprintf(stderr, "Problems sending mail to %s: %s\n", dest,
                  StrError(errno).c_str());
        }
      }
      return ok;
    } else {
      if (use_logging) {
        LOG(ERROR) << "Unable to send mail to " << dest;
      } else {
        fprintf(stderr, "Unable to send mail to %s\n", dest);
      }
    }
  }
#else
  (void)dest;
  (void)subject;
  (void)body;
  (void)use_logging;
  LOG(WARNING) << "Email support not available; not sending message";
#endif
  return false;
}

bool SendEmail(const char* dest, const char* subject, const char* body) {
  return SendEmailInternal(dest, subject, body, true);
}

static void GetTempDirectories(vector<string>& list) {
  list.clear();
#ifdef NGLOG_OS_WINDOWS
  // On windows we'll try to find a directory in this order:
  //   C:/Documents & Settings/whomever/TEMP (or whatever GetTempPath() is)
  //   C:/TMP/
  //   C:/TEMP/
  char tmp[MAX_PATH];
  if (GetTempPathA(MAX_PATH, tmp)) list.push_back(tmp);
  list.push_back("C:\\TMP\\");
  list.push_back("C:\\TEMP\\");
#else
  // Directories, in order of preference. If we find a dir that
  // exists, we stop adding other less-preferred dirs
  const char* candidates[] = {
      // Non-null only during unittest/regtest
      getenv("TEST_TMPDIR"),

      // Explicitly-supplied temp dirs
      getenv("TMPDIR"),
      getenv("TMP"),

      // If all else fails
      "/tmp",
  };

  for (auto d : candidates) {
    if (!d) continue;  // Empty env var

    // Make sure we don't surprise anyone who's expecting a '/'
    string dstr = d;
    if (dstr[dstr.size() - 1] != '/') {
      dstr += "/";
    }
    list.push_back(dstr);

    struct stat statbuf;
    if (!stat(d, &statbuf) && S_ISDIR(statbuf.st_mode)) {
      // We found a dir that exists - we're done.
      return;
    }
  }
#endif
}

static std::unique_ptr<std::vector<std::string>> logging_directories_list;

const vector<string>& GetLoggingDirectories() {
  // Not strictly thread-safe but we're called early in InitGoogle().
  if (logging_directories_list == nullptr) {
    logging_directories_list = std::make_unique<std::vector<std::string>>();

    if (!FLAGS_log_dir.empty()) {
      // Ensure the specified path ends with a directory delimiter.
      if (std::find(std::begin(possible_dir_delim),
                    std::end(possible_dir_delim),
                    FLAGS_log_dir.back()) == std::end(possible_dir_delim)) {
        logging_directories_list->push_back(FLAGS_log_dir + "/");
      } else {
        logging_directories_list->push_back(FLAGS_log_dir);
      }
    } else {
      GetTempDirectories(*logging_directories_list);
#ifdef NGLOG_OS_WINDOWS
      char tmp[MAX_PATH];
      if (GetWindowsDirectoryA(tmp, MAX_PATH))
        logging_directories_list->push_back(tmp);
      logging_directories_list->push_back(".\\");
#else
      logging_directories_list->push_back("./");
#endif
    }
  }
  return *logging_directories_list;
}

// Returns a set of existing temporary directories, which will be a
// subset of the directories returned by GetLoggingDirectories().
// Thread-safe.
NGLOG_NO_EXPORT
void GetExistingTempDirectories(vector<string>& list) {
  GetTempDirectories(list);
  auto i_dir = list.begin();
  while (i_dir != list.end()) {
    // zero arg to access means test for existence; no constant
    // defined on windows
    if (access(i_dir->c_str(), 0)) {
      i_dir = list.erase(i_dir);
    } else {
      ++i_dir;
    }
  }
}

void TruncateLogFile(const char* path, uint64 limit, uint64 keep) {
#if defined(HAVE_UNISTD_H) || defined(HAVE__CHSIZE_S)
  struct stat statbuf;
  const int kCopyBlockSize = 8 << 10;
  char copybuf[kCopyBlockSize];
  off_t read_offset, write_offset;
  // Don't follow symlinks unless they're our own fd symlinks in /proc
  int flags = O_RDWR;
  // TODO(hamaji): Support other environments.
#  ifdef NGLOG_OS_LINUX
  const char* procfd_prefix = "/proc/self/fd/";
  if (strncmp(procfd_prefix, path, strlen(procfd_prefix))) flags |= O_NOFOLLOW;
#  endif

  FileDescriptor fd{open(path, flags)};
  if (!fd) {
    if (errno == EFBIG) {
      // The log file in question has got too big for us to open. The
      // real fix for this would be to compile logging.cc (or probably
      // all of base/...) with -D_FILE_OFFSET_BITS=64 but that's
      // rather scary.
      // Instead just truncate the file to something we can manage
#  ifdef HAVE__CHSIZE_S
      if (_chsize_s(fd.get(), 0) != 0) {
#  else
      if (truncate(path, 0) == -1) {
#  endif
        PLOG(ERROR) << "Unable to truncate " << path;
      } else {
        LOG(ERROR) << "Truncated " << path << " due to EFBIG error";
      }
    } else {
      PLOG(ERROR) << "Unable to open " << path;
    }
    return;
  }

  if (fstat(fd.get(), &statbuf) == -1) {
    PLOG(ERROR) << "Unable to fstat()";
    return;
  }

  // See if the path refers to a regular file bigger than the
  // specified limit
  if (!S_ISREG(statbuf.st_mode)) return;
  if (statbuf.st_size <= static_cast<off_t>(limit)) return;
  if (statbuf.st_size <= static_cast<off_t>(keep)) return;

  // This log file is too large - we need to truncate it
  LOG(INFO) << "Truncating " << path << " to " << keep << " bytes";

  // Copy the last "keep" bytes of the file to the beginning of the file
  read_offset = statbuf.st_size - static_cast<off_t>(keep);
  write_offset = 0;
  ssize_t bytesin, bytesout;
  while ((bytesin = pread(fd.get(), copybuf, sizeof(copybuf), read_offset)) >
         0) {
    bytesout =
        pwrite(fd.get(), copybuf, static_cast<size_t>(bytesin), write_offset);
    if (bytesout == -1) {
      PLOG(ERROR) << "Unable to write to " << path;
      break;
    } else if (bytesout != bytesin) {
      LOG(ERROR) << "Expected to write " << bytesin << ", wrote " << bytesout;
    }
    read_offset += bytesin;
    write_offset += bytesout;
  }
  if (bytesin == -1) PLOG(ERROR) << "Unable to read from " << path;

  // Truncate the remainder of the file. If someone else writes to the
  // end of the file after our last read() above, we lose their latest
  // data. Too bad ...
#  ifdef HAVE__CHSIZE_S
  if (_chsize_s(fd.get(), write_offset) != 0) {
#  else
  if (ftruncate(fd.get(), write_offset) == -1) {
#  endif
    PLOG(ERROR) << "Unable to truncate " << path;
  }

#else
  LOG(ERROR) << "No log truncation support.";
#endif
}

void TruncateStdoutStderr() {
#ifdef HAVE_UNISTD_H
  uint64 limit = MaxLogSize() << 20U;
  uint64 keep = 1U << 20U;
  TruncateLogFile("/proc/self/fd/1", limit, keep);
  TruncateLogFile("/proc/self/fd/2", limit, keep);
#else
  LOG(ERROR) << "No log truncation support.";
#endif
}

namespace internal {
// Helper functions for string comparisons.
#define DEFINE_CHECK_STROP_IMPL(name, func, expected)                         \
  std::unique_ptr<string> Check##func##expected##Impl(                        \
      const char* s1, const char* s2, const char* names) {                    \
    bool equal = s1 == s2 || (s1 && s2 && !func(s1, s2));                     \
    if (equal == (expected))                                                  \
      return nullptr;                                                         \
    else {                                                                    \
      ostringstream ss;                                                       \
      if (!s1) s1 = "";                                                       \
      if (!s2) s2 = "";                                                       \
      ss << #name " failed: " << names << " (" << s1 << " vs. " << s2 << ")"; \
      return std::make_unique<std::string>(ss.str());                         \
    }                                                                         \
  }
DEFINE_CHECK_STROP_IMPL(CHECK_STREQ, strcmp, true)
DEFINE_CHECK_STROP_IMPL(CHECK_STRNE, strcmp, false)
DEFINE_CHECK_STROP_IMPL(CHECK_STRCASEEQ, strcasecmp, true)
DEFINE_CHECK_STROP_IMPL(CHECK_STRCASENE, strcasecmp, false)
#undef DEFINE_CHECK_STROP_IMPL
}  // namespace internal

// glibc has traditionally implemented two incompatible versions of
// strerror_r(). There is a poorly defined convention for picking the
// version that we want, but it is not clear whether it even works with
// all versions of glibc.
// So, instead, we provide this wrapper that automatically detects the
// version that is in use, and then implements POSIX semantics.
// N.B. In addition to what POSIX says, we also guarantee that "buf" will
// be set to an empty string, if this function failed. This means, in most
// cases, you do not need to check the error code and you can directly
// use the value of "buf". It will never have an undefined value.
// DEPRECATED: Use StrError(int) instead.
NGLOG_NO_EXPORT
int posix_strerror_r(int err, char* buf, size_t len) {
  // Sanity check input parameters
  if (buf == nullptr || len <= 0) {
    errno = EINVAL;
    return -1;
  }

  // Reset buf and errno, and try calling whatever version of strerror_r()
  // is implemented by glibc
  buf[0] = '\000';
  int old_errno = errno;
  errno = 0;
  char* rc = reinterpret_cast<char*>(strerror_r(err, buf, len));

  // Both versions set errno on failure
  if (errno) {
    // Should already be there, but better safe than sorry
    buf[0] = '\000';
    return -1;
  }
  errno = old_errno;

  // POSIX is vague about whether the string will be terminated, although
  // is indirectly implies that typically ERANGE will be returned, instead
  // of truncating the string. This is different from the GNU implementation.
  // We play it safe by always terminating the string explicitly.
  buf[len - 1] = '\000';

  // If the function succeeded, we can use its exit code to determine the
  // semantics implemented by glibc
  if (!rc) {
    return 0;
  } else {
    // GNU semantics detected
    if (rc == buf) {
      return 0;
    } else {
      buf[0] = '\000';
#if defined(NGLOG_OS_MACOSX) || defined(NGLOG_OS_FREEBSD) || \
    defined(NGLOG_OS_OPENBSD)
      if (reinterpret_cast<intptr_t>(rc) < sys_nerr) {
        // This means an error on MacOSX or FreeBSD.
        return -1;
      }
#endif
      strncat(buf, rc, len - 1);
      return 0;
    }
  }
}

// A thread-safe replacement for strerror(). Returns a string describing the
// given POSIX error code.
string StrError(int err) {
  char buf[100];
  int rc = posix_strerror_r(err, buf, sizeof(buf));
  if ((rc < 0) || (buf[0] == '\000')) {
    const int written = std::snprintf(buf, sizeof(buf), "Error number %d", err);
    if (written < 0 || static_cast<std::size_t>(written) >= sizeof(buf)) {
      buf[0] = '\0';
    }
  }
  return buf;
}

LogMessageFatal::LogMessageFatal(const char* file, int line)
    : LogMessage(file, line, NGLOG_FATAL) {}

LogMessageFatal::LogMessageFatal(const char* file, int line,
                                 const internal::CheckOpString& result)
    : LogMessage(file, line, result) {}

LogMessageFatal::~LogMessageFatal() noexcept(false) {
  Flush();
  LogMessage::Fail();
}

namespace internal {

CheckOpMessageBuilder::CheckOpMessageBuilder(const char* exprtext)
    : stream_(new ostringstream) {
  *stream_ << exprtext << " (";
}

CheckOpMessageBuilder::~CheckOpMessageBuilder() { delete stream_; }

ostream* CheckOpMessageBuilder::ForVar2() {
  *stream_ << " vs. ";
  return stream_;
}

std::unique_ptr<string> CheckOpMessageBuilder::NewString() {
  *stream_ << ")";
  return std::make_unique<std::string>(stream_->str());
}

template <>
void MakeCheckOpValueString(std::ostream* os, const char& v) {
  if (v >= 32 && v <= 126) {
    (*os) << "'" << v << "'";
  } else {
    (*os) << "char value " << static_cast<short>(v);
  }
}

template <>
void MakeCheckOpValueString(std::ostream* os, const signed char& v) {
  if (v >= 32 && v <= 126) {
    (*os) << "'" << v << "'";
  } else {
    (*os) << "signed char value " << static_cast<short>(v);
  }
}

template <>
void MakeCheckOpValueString(std::ostream* os, const unsigned char& v) {
  if (v >= 32 && v <= 126) {
    (*os) << "'" << v << "'";
  } else {
    (*os) << "unsigned char value " << static_cast<unsigned short>(v);
  }
}

template <>
void MakeCheckOpValueString(std::ostream* os, const std::nullptr_t& /*v*/) {
  (*os) << "nullptr";
}

}  // namespace internal

void InitializeLogging(const char* argv0) { InitializeLoggingUtilities(argv0); }

void InstallPrefixFormatter(PrefixFormatterCallback callback, void* data) {
  if (callback != nullptr) {
    g_prefix_formatter = std::make_unique<PrefixFormatter>(callback, data);
  } else {
    g_prefix_formatter = nullptr;
  }
}

void ShutdownLogging() {
  ShutdownLoggingUtilities();
  LogDestination::DeleteLogDestinations();
  logging_directories_list = nullptr;
  g_prefix_formatter = nullptr;
}

void EnableLogCleaner(unsigned int overdue_days) {
  internal::g_log_cleaner.Enable(
      std::chrono::duration_cast<std::chrono::minutes>(
          std::chrono::duration<unsigned, std::ratio<kSecondsInDay>>{
              overdue_days}));
}

void EnableLogCleaner(const std::chrono::minutes& overdue) {
  internal::g_log_cleaner.Enable(overdue);
}

void DisableLogCleaner() { internal::g_log_cleaner.Disable(); }

LogMessageTime::LogMessageTime() = default;

namespace {

template <class... Args>
struct void_impl {
  using type = void;
};

template <class... Args>
using void_t = typename void_impl<Args...>::type;

template <class T, class E = void>
struct has_member_tm_gmtoff : std::false_type {};

template <class T>
struct has_member_tm_gmtoff<T, void_t<decltype(&T::tm_gmtoff)>>
    : std::true_type {};

template <class T, bool = has_member_tm_gmtoff<T>::value>
struct BreakdownImpl {
  static std::tuple<std::tm, std::time_t, std::chrono::minutes> Get(
      const std::chrono::system_clock::time_point& now) {
    std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
    std::tm tm_local;
    std::tm tm_utc;
    int isdst = 0;

    if (FLAGS_log_utc_time) {
      gmtime_r(&timestamp, &tm_local);
      localtime_r(&timestamp, &tm_utc);
      isdst = tm_utc.tm_isdst;
      tm_utc = tm_local;
    } else {
      localtime_r(&timestamp, &tm_local);
      isdst = tm_local.tm_isdst;
      gmtime_r(&timestamp, &tm_utc);
    }

    std::time_t gmt_sec = std::mktime(&tm_utc);

    // If the Daylight Saving Time(isDst) is active subtract an hour from the
    // current timestamp.
    using namespace std::chrono_literals;
    const auto gmtoffset = std::chrono::duration_cast<std::chrono::minutes>(
        std::chrono::system_clock::from_time_t(timestamp) -
        std::chrono::system_clock::from_time_t(gmt_sec) + (isdst ? 1h : 0h));

    return std::make_tuple(tm_local, timestamp, gmtoffset);
  }
};

template <class T>
struct BreakdownImpl<T, true> {
  static std::tuple<std::tm, std::time_t, std::chrono::minutes> Get(
      const std::chrono::system_clock::time_point& now) {
    std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
    T tm;

    if (FLAGS_log_utc_time) {
      gmtime_r(&timestamp, &tm);
    } else {
      localtime_r(&timestamp, &tm);
    }

    const auto gmtoffset = std::chrono::duration_cast<std::chrono::minutes>(
        std::chrono::seconds{tm.tm_gmtoff});

    return std::make_tuple(tm, timestamp, gmtoffset);
  }
};

auto Breakdown(const std::chrono::system_clock::time_point& now)
    -> std::tuple<std::tm, std::time_t, std::chrono::minutes> {
  return BreakdownImpl<std::tm>::Get(now);
}

}  // namespace

LogMessageTime::LogMessageTime(std::chrono::system_clock::time_point now)
    : timestamp_{now} {
  std::time_t timestamp;
  std::tie(tm_, timestamp, gmtoffset_) = Breakdown(now);
  usecs_ = std::chrono::duration_cast<std::chrono::microseconds>(
      now - std::chrono::system_clock::from_time_t(timestamp));
}

}  // namespace nglog
