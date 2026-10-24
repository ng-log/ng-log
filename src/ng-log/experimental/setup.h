// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Initializes, configures and shuts down logging for the experimental logging
// functions. Strings are passed by length, so they need not be
// null-terminated. Names of files and of the mailer's addresses must not
// contain null characters.

#ifndef NGLOG_EXPERIMENTAL_SETUP_H
#define NGLOG_EXPERIMENTAL_SETUP_H

#include "ng-log/experimental/requirements.h"  // IWYU pragma: keep

#if defined(NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_SUPPORTED)

#  include <filesystem>
#  include <string>
#  include <string_view>
#  include <vector>

#  include "ng-log/experimental/attributes.h"
#  include "ng-log/experimental/sink.h"
#  include "ng-log/export.h"

namespace nglog::experimental {

// Initializes logging. Messages name the program after the part of the program
// name after the last path separator, which is copied.
NGLOG_EXPORT void Initialize(std::string_view program_name);

// Whether Initialize() was called without a later Shutdown().
NGLOG_EXPORT bool IsInitialized() noexcept;

// Shuts down logging.
NGLOG_EXPORT void Shutdown();

// Sets the level up to which Verbose messages are logged, like --v.
NGLOG_EXPORT void SetVerbosity(int level);

// Sets the level up to which Verbose messages are logged in the source files
// whose base name without extension matches the pattern, like --vmodule. The
// pattern supports the wildcards * and ?. Returns the level that applied to the
// pattern before.
NGLOG_EXPORT int SetVerbosity(std::string_view module_pattern, int level);

// Logs messages of at least the severity to the standard error as well, like
// --stderrthreshold.
NGLOG_EXPORT void SetStderrThreshold(Severity severity);

// Logs every message to the standard error only and stops writing log files.
NGLOG_EXPORT void LogOnlyToStderr();

// Writes messages of the severity to log files whose names start with the base
// file name, which must not be empty.
NGLOG_EXPORT void SetLogFile(Severity severity,
                             const std::filesystem::path& base_filename);

// Stops writing messages of the severity to a log file.
NGLOG_EXPORT void DisableLogFile(Severity severity);

// Names the link to the latest log file of the severity. The name must be a
// file name without directories. The link is named after the program by
// default.
NGLOG_EXPORT void SetLogFileLink(Severity severity,
                                 const std::filesystem::path& link_name);

// Stops linking to the latest log file of the severity.
NGLOG_EXPORT void DisableLogFileLink(Severity severity);

// Appends the suffix to the names of the log files of all severities, such as
// to distinguish processes by the port they listen on.
NGLOG_EXPORT void SetLogFileSuffix(std::string_view suffix);

// Sends messages of at least the severity by email to the addresses, which are
// separated by commas.
NGLOG_EXPORT void SetEmailThreshold(Severity severity,
                                    std::string_view addresses);

// Flushes the log files of at least the severity once the logging thread wrote
// the messages that Async passed to it.
NGLOG_EXPORT void Flush(Severity severity);

// Name of the severity as the prefix of a message shows it, such as WARNING.
NGLOG_EXPORT std::string_view Name(Severity severity) noexcept;

// Directories tried in order for log files that --log_dir does not place.
NGLOG_EXPORT std::vector<std::filesystem::path> LogDirectories();

// Identifies the build of the application in the header of log files.
NGLOG_EXPORT void SetFingerprint(std::string_view fingerprint);

// Function called after a fatal message instead of terminating the program
// with a stack trace.
using FatalHandler = void (*)();

// Sets the function called after a fatal message, which must not be null.
// Returns the previous one.
NGLOG_EXPORT FatalHandler SetFatalHandler(FatalHandler handler);

// Function that writes the failure dump of the failure signal handler. It is
// called from the signal handler and must therefore be async-signal-safe.
using FailureWriter = void (*)(std::string_view data);

// Sets the function that writes the failure dump, which must not be null.
NGLOG_EXPORT void SetFailureWriter(FailureWriter writer);

// Sends every message the experimental logging functions log to the log files
// to the structured sink as well, until RemoveSink(). The sink must outlive its
// registration and must not be added twice.
NGLOG_EXPORT void AddSink(Sink& sink);

// Stops sending messages to a sink that AddSink() added. Returns once no other
// thread sends a message to the sink.
NGLOG_EXPORT void RemoveSink(Sink& sink);

// Stack trace of the caller, one frame per line.
NGLOG_EXPORT std::string StackTrace();

}  // namespace nglog::experimental

#endif  // defined(NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_SUPPORTED)

#endif  // NGLOG_EXPERIMENTAL_SETUP_H
