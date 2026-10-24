// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Declares the functions that initialize, configure and shut down logging.

#ifndef NGLOG_LOG_SETUP_H
#define NGLOG_LOG_SETUP_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "ng-log/export.h"
#include "ng-log/log_severity.h"
#include "ng-log/platform.h"

namespace nglog {

#ifdef NGLOG_OS_WINDOWS
inline namespace tools {
NGLOG_EXPORT std::string FormatWindowsMessage(std::uint32_t error);
}
#endif

// Initialize logging. You will see the program name specified by argv0 in log
// outputs.
NGLOG_EXPORT void InitializeLogging(const char* argv0);

// Check if logging has been initialized.
NGLOG_EXPORT bool IsLoggingInitialized();

// Shutdown logging.
NGLOG_EXPORT void ShutdownLogging();

typedef void (*logging_fail_func_t)();

class LogMessage;

using PrefixFormatterCallback = void (*)(std::ostream&, const LogMessage&,
                                         void*);

NGLOG_EXPORT void InstallPrefixFormatter(PrefixFormatterCallback callback,
                                         void* data = nullptr);

// Install a function which will be called after LOG(FATAL). Returns the
// previously set function.
NGLOG_EXPORT logging_fail_func_t
InstallFailureFunction(logging_fail_func_t fail_func);

// Enable/Disable old log cleaner.
NGLOG_EXPORT void EnableLogCleaner(const std::chrono::minutes& overdue);
NGLOG_EXPORT void DisableLogCleaner();
NGLOG_EXPORT void SetApplicationFingerprint(const std::string& fingerprint);

// Flushes all log files that contains messages that are at least of
// the specified severity level.  Thread-safe.
NGLOG_EXPORT void FlushLogFiles(LogSeverity min_severity);

// Flushes all log files that contains messages that are at least of
// the specified severity level. Thread-hostile because it ignores
// locking -- used for catastrophic failures.
NGLOG_EXPORT void FlushLogFilesUnsafe(LogSeverity min_severity);

//
// Set the destination to which a particular severity level of log
// messages is sent.  If base_filename is "", it means "don't log this
// severity".  Thread-safe.
//
NGLOG_EXPORT void SetLogDestination(LogSeverity severity,
                                    const char* base_filename);

//
// Set the basename of the symlink to the latest log file at a given
// severity.  If symlink_basename is empty, do not make a symlink.  If
// you don't call this function, the symlink basename is the
// invocation name of the program.  On Windows, the link is a shell
// shortcut whose name ends in ".lnk".  Thread-safe.
//
NGLOG_EXPORT void SetLogSymlink(LogSeverity severity,
                                const char* symlink_basename);

//
// Specify an "extension" added to the filename specified via
// SetLogDestination.  This applies to all severity levels.  It's
// often used to append the port we're listening on to the logfile
// name.  Thread-safe.
//
NGLOG_EXPORT void SetLogFilenameExtension(const char* filename_extension);

//
// Make it so that all log messages of at least a particular severity
// are logged to stderr (in addition to logging to the usual log
// file(s)).  Thread-safe.
//
NGLOG_EXPORT void SetStderrLogging(LogSeverity min_severity);

//
// Make it so that all log messages go only to stderr.  Thread-safe.
//
NGLOG_EXPORT void LogToStderr();

//
// Make it so that all log messages of at least a particular severity are
// logged via email to a list of addresses (in addition to logging to the
// usual log file(s)).  The list of addresses is just a string containing
// the email addresses to send to (separated by spaces, say).  Thread-safe.
//
NGLOG_EXPORT void SetEmailLogging(LogSeverity min_severity,
                                  const char* addresses);

// A simple function that sends email. dest is a comma-separated
// list of addresses.  Thread-safe.
NGLOG_EXPORT bool SendEmail(const char* dest, const char* subject,
                            const char* body);

NGLOG_EXPORT const std::vector<std::string>& GetLoggingDirectories();

// Print any fatal message again -- useful to call from signal handler
// so that the last thing in the output is the fatal message.
// Thread-hostile, but a race is unlikely.
NGLOG_EXPORT void ReprintFatalMessage();

// Truncate a log file that may be the append-only output of multiple
// processes and hence can't simply be renamed/reopened (typically a
// stdout/stderr).  If the file "path" is > "limit" bytes, copy the
// last "keep" bytes to offset 0 and truncate the rest. Since we could
// be racing with other writers, this approach has the potential to
// lose very small amounts of data. For security, only follow symlinks
// if the path is /proc/self/fd/*
NGLOG_EXPORT void TruncateLogFile(const char* path, std::uint64_t limit,
                                  std::uint64_t keep);

// Truncate stdout and stderr if they are over the value specified by
// --max_log_size. Keep the final 1 MiB. This function has the same
// race condition as TruncateLogFile.
NGLOG_EXPORT void TruncateStdoutStderr();

// Return the string representation of the provided LogSeverity level.
// Thread-safe.
NGLOG_EXPORT const char* GetLogSeverityName(LogSeverity severity);

// ---------------------------------------------------------------------
// Implementation details that are not useful to most clients
// ---------------------------------------------------------------------

// A Logger is the interface used by logging modules to emit entries
// to a log.  A typical implementation will dump formatted data to a
// sequence of files.  We also provide interfaces that will forward
// the data to another thread so that the invoker never blocks.
// Implementations should be thread-safe since the logging system
// will write to them from multiple threads.

namespace base {

class NGLOG_EXPORT Logger {
 public:
  virtual ~Logger();

  // Writes "message[0,message_len-1]" corresponding to an event that
  // occurred at "timestamp".  If "force_flush" is true, the log file
  // is flushed immediately.
  //
  // The input message has already been formatted as deemed
  // appropriate by the higher level logging facility.  For example,
  // textual log messages already contain timestamps, and the
  // file:linenumber header.
  virtual void Write(bool force_flush,
                     const std::chrono::system_clock::time_point& timestamp,
                     const char* message, std::size_t message_len) = 0;

  // Flush any buffered messages
  virtual void Flush() = 0;

  // Get the current LOG file size.
  // The returned value is approximate since some
  // logged data may not have been flushed to disk yet.
  virtual std::uint32_t LogSize() = 0;
};

// Get the logger for the specified severity level.  The logger
// remains the property of the logging module and should not be
// deleted by the caller.  Thread-safe.
extern NGLOG_EXPORT Logger* GetLogger(LogSeverity level);

// Set the logger for the specified severity level.  The logger
// becomes the property of the logging module and should not
// be deleted by the caller.  Thread-safe.
extern NGLOG_EXPORT void SetLogger(LogSeverity level, Logger* logger);

}  // namespace base

// Install a signal handler that will dump signal information and a stack
// trace when the program crashes on certain signals.  We'll install the
// signal handler for the following signals.
//
// SIGSEGV, SIGILL, SIGFPE, SIGABRT, SIGBUS, and SIGTERM.
//
// By default, the signal handler will write the failure dump to the
// standard error.  You can customize the destination by installing your
// own writer function by InstallFailureWriter() below.
//
// Note on threading:
//
// The function should be called before threads are created, if you want
// to use the failure signal handler for all threads.  The stack trace
// will be shown only for the thread that receives the signal.  In other
// words, stack traces of other threads won't be shown.
NGLOG_EXPORT void InstallFailureSignalHandler();

// Returns true if InstallFailureSignalHandler() has installed the failure
// signal handler. Signal dispositions changed afterwards do not affect the
// result.
NGLOG_EXPORT bool IsFailureSignalHandlerInstalled();

// Installs a function that is used for writing the failure dump.  "data"
// is the pointer to the beginning of a message to be written, and "size"
// is the size of the message.  You should not expect the data is
// terminated with '\0'.
NGLOG_EXPORT void InstallFailureWriter(void (*writer)(const char* data,
                                                      std::size_t size));

// Dump stack trace as a string. It lists at most 32 frames and ends with the
// line "(More stack frames follow...)" if the stack is deeper.
NGLOG_EXPORT std::string GetStackTrace();

}  // namespace nglog

#endif  // NGLOG_LOG_SETUP_H
