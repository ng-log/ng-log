// SPDX-FileCopyrightText: 2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ng-log/flags.h"

#include <cstdint>
#include <cstdlib>
#include <string>

#include "internal/flag_display.h"
#include "ng-log/log_severity.h"

namespace {

constexpr std::int32_t kSecondsPerMinute = 60;
constexpr std::int32_t kDefaultLogBufferSeconds = 30;
constexpr std::int32_t kDefaultLogCleanupSeconds = 5 * kSecondsPerMinute;
// Exceeds every severity level, which disables email by default.
constexpr std::int32_t kDefaultEmailLevel = 999;
constexpr std::uint32_t kDefaultLogFileMode = 0664;
constexpr std::uint32_t kDefaultMaxLogSizeMiB = 1800;
constexpr std::int32_t kDefaultAddr2lineTimeoutMs = 2000;

// Compute the default value for --log_dir
static const char* DefaultLogDir() {
  constexpr const char* const names[]{"NGLOG_DIR", "TEST_TMPDIR"};
  for (const char* const name : names) {
    const char* const env = std::getenv(name);
    if (env != nullptr && env[0] != '\0') {
      return env;
    }
  }
  return "";
}

void WriteLogToStderrHelp(nglog::HelpWriter& help) {
  help.Write("Send log messages to ");
  help.Write(nglog::HelpTextStyle::kStream, "stderr");
  help.Write(" instead of log files.");
}

void WriteAlsoLogToStderrHelp(nglog::HelpWriter& help) {
  help.Write("Also send log messages to ");
  help.Write(nglog::HelpTextStyle::kStream, "stderr");
  help.Write(" in addition to the log files.");
}

void WriteColorLogToStderrHelp(nglog::HelpWriter& help) {
  help.Write("Color log messages written to ");
  help.Write(nglog::HelpTextStyle::kStream, "stderr");
  help.Write(". Coloring is disabled by ");
  help.Write(nglog::HelpTextStyle::kEnvironmentVariable, "NO_COLOR");
  help.Write(" or ");
  help.Write(nglog::HelpTextStyle::kEnvironmentVariable, "TERM");
  help.Write("=");
  help.Write(nglog::HelpTextStyle::kValue, "dumb");
  help.Write(".");
}

void WriteColorLogToStdoutHelp(nglog::HelpWriter& help) {
  help.Write("Color log messages written to ");
  help.Write(nglog::HelpTextStyle::kStream, "stdout");
  help.Write(". Coloring is disabled by ");
  help.Write(nglog::HelpTextStyle::kEnvironmentVariable, "NO_COLOR");
  help.Write(" or ");
  help.Write(nglog::HelpTextStyle::kEnvironmentVariable, "TERM");
  help.Write("=");
  help.Write(nglog::HelpTextStyle::kValue, "dumb");
  help.Write(".");
}

void WriteLogToStdoutHelp(nglog::HelpWriter& help) {
  help.Write("Send log messages to ");
  help.Write(nglog::HelpTextStyle::kStream, "stdout");
  help.Write(" instead of log files.");
}

void WriteStderrThresholdHelp(nglog::HelpWriter& help) {
  help.Write("Copy log messages at or above this severity level to ");
  help.Write(nglog::HelpTextStyle::kStream, "stderr");
  help.Write(" in addition to the log files. This flag supersedes ");
  help.Write(nglog::HelpTextStyle::kOptionPrefix, "--");
  help.Write(nglog::HelpTextStyle::kOption, "alsologtostderr");
  help.Write(".");
}

void WriteLogBacktraceAtHelp(nglog::HelpWriter& help) {
  help.Write("Emit a backtrace when logging from ");
  help.Write(nglog::HelpTextStyle::kMeta, "file:line");
  help.Write(".");
}

void WriteVHelp(nglog::HelpWriter& help) {
  help.Write(
      "Show VLOG(m) messages when m is at most this value. Override "
      "with ");
  help.Write(nglog::HelpTextStyle::kOptionPrefix, "--");
  help.Write(nglog::HelpTextStyle::kOption, "vmodule");
  help.Write(".");
}

void WriteVmoduleHelp(nglog::HelpWriter& help) {
  help.Write("Set per-module verbose levels with a comma-separated list of ");
  help.Write(nglog::HelpTextStyle::kValue, "<module name>=<log level>");
  help.Write(". ");
  help.Write(nglog::HelpTextStyle::kValue, "<module name>");
  help.Write(
      " is a glob pattern matched against the source file base name before "
      "its first period, with a trailing ");
  help.Write(nglog::HelpTextStyle::kValue, "-inl");
  help.Write(" suffix removed. ");
  help.Write(nglog::HelpTextStyle::kValue, "<log level>");
  help.Write(" overrides ");
  help.Write(nglog::HelpTextStyle::kOptionPrefix, "--");
  help.Write(nglog::HelpTextStyle::kOption, "v");
  help.Write(".");
}

void WriteSymbolizeLineInfoHelp(nglog::HelpWriter& help) {
  help.Write(
      "Resolve source file names and line numbers in stack frames with ");
  help.Write(nglog::HelpTextStyle::kExecutable, "addr2line");
  help.Write(" or ");
  help.Write(nglog::HelpTextStyle::kExecutable, "libbacktrace");
  help.Write(", when available.");
}

void WriteSymbolizeHyperlinksHelp(nglog::HelpWriter& help) {
  help.Write("Add hyperlinks to ");
  help.Write(nglog::HelpTextStyle::kMeta, "file:line");
  help.Write(
      " references in colorized stack traces when supported by the "
      "terminal. Set ");
  help.Write(nglog::HelpTextStyle::kConstant, "false");
  help.Write(" to disable them.");
}

void WriteAddr2lineTimeoutHelp(nglog::HelpWriter& help) {
  help.Write("Wait no longer than this many milliseconds for ");
  help.Write(nglog::HelpTextStyle::kExecutable, "addr2line");
  help.Write(" to resolve one address.");
}

}  // namespace

NGLOG_DEFINE(bool, timestamp_in_logfile_name, true,
             "Add a timestamp to each log file name.");
NGLOG_DEFINE(bool, logtostderr, false, WriteLogToStderrHelp);
NGLOG_DEFINE(bool, alsologtostderr, false, WriteAlsoLogToStderrHelp);
NGLOG_DEFINE(bool, colorlogtostderr, true, WriteColorLogToStderrHelp);
NGLOG_DEFINE(bool, colorlogtostdout, true, WriteColorLogToStdoutHelp);
NGLOG_DEFINE(bool, logtostdout, false, WriteLogToStdoutHelp);
#ifdef NGLOG_OS_LINUX
NGLOG_DEFINE(
    bool, drop_log_memory, true,
    "Drop in-memory buffers of log contents. "
    "Logs can grow very quickly and they are rarely read before they "
    "need to be evicted from memory. Instead, release them from memory "
    "as soon as they are flushed to disk.");
#endif

// By default, errors (including fatal errors) get logged to stderr as
// well as the file.
//
// The default is ERROR instead of FATAL so that users can see problems
// when they run a program without having to look in another file.
NGLOG_DEFINE(std::int32_t, stderrthreshold, NGLOG_ERROR,
             WriteStderrThresholdHelp);

NGLOG_DEFINE(std::string, alsologtoemail, "",
             "Send log messages to each address in this comma-separated list "
             "in addition "
             "to log files.");
NGLOG_DEFINE(bool, log_file_header, true,
             "Write a header at the beginning of each log file.");
NGLOG_DEFINE(bool, log_prefix, true,
             "Add the log prefix to the beginning of each log line.");
NGLOG_DEFINE(bool, log_year_in_prefix, true,
             "Include the year in each log line's prefix.");
NGLOG_DEFINE(std::int32_t, minloglevel, 0,
             "Discard messages below this severity level.");
NGLOG_DEFINE(std::int32_t, logbuflevel, 0,
             "Buffer messages at this severity level or below.");
NGLOG_DEFINE(std::int32_t, logbufsecs, kDefaultLogBufferSeconds,
             "Keep buffered log messages for no more than this many seconds.");
NGLOG_DEFINE(
    std::int32_t, logcleansecs, kDefaultLogCleanupSeconds,
    "Check for and clean up overdue log files every this many seconds.");
NGLOG_DEFINE(std::int32_t, logemaillevel, kDefaultEmailLevel,
             "Email messages at this severity level or above.");
NGLOG_DEFINE(std::string, logmailer, "",
             "Use this program to send logging email.");
NGLOG_DEFINE(nglog::FileMode, logfile_mode,
             nglog::FileMode{kDefaultLogFileMode},
             "Set the permissions for newly created log files.");
NGLOG_DEFINE(std::string, log_dir, DefaultLogDir(),
             "Write log files to this directory instead of the default logging "
             "directory.");
NGLOG_DEFINE(std::string, log_link, "",
             "Create additional links to log files in this directory.");
NGLOG_DEFINE(std::uint32_t, max_log_size, kDefaultMaxLogSizeMiB,
             "Limit each log file to approximately this size in MiB. A value "
             "of 0 is treated as 1 and values above 4095 are capped at 4095.");
NGLOG_DEFINE(bool, stop_logging_if_full_disk, false,
             "Stop writing log files when the disk is full.");
NGLOG_DEFINE(std::string, log_backtrace_at, "", WriteLogBacktraceAtHelp);
NGLOG_DEFINE(bool, log_utc_time, false, "Use UTC timestamps in log messages.");
NGLOG_DEFINE(std::int32_t, v, 0, WriteVHelp);
NGLOG_DEFINE(std::string, vmodule, "", WriteVmoduleHelp);
NGLOG_DEFINE(bool, symbolize_stacktrace, true,
             "Symbolize stack traces in crash reports.");
NGLOG_DEFINE(std::string, symbolize_file_base_path, "",
             "Use this base path to resolve relative source paths when "
             "creating terminal hyperlinks.");
NGLOG_DEFINE(bool, symbolize_line_info, true, WriteSymbolizeLineInfoHelp);
NGLOG_DEFINE(bool, symbolize_hyperlinks, true, WriteSymbolizeHyperlinksHelp);

NGLOG_DEFINE(std::int32_t, addr2line_timeout_ms, kDefaultAddr2lineTimeoutMs,
             WriteAddr2lineTimeoutHelp);

namespace {

const nglog::FlagModule kNglogFlagModule("ng-log", "Options provided by",
                                         "NGLOG_");

struct FlagMetadataInitializer {
  FlagMetadataInitializer() {
    const auto logging_group = kNglogFlagModule.AddGroup("Logging");
    const auto verbose_logging_group =
        kNglogFlagModule.AddGroup("Verbose logging");
    const auto stack_traces_group = kNglogFlagModule.AddGroup("Stack traces");
    struct Metadata {
      nglog::FlagBase* flag;
      const nglog::FlagGroup* group;
      // Overrides the type shown by the generated help.
      const char* type = nullptr;
      // Unit appended to the default value shown by the generated help.
      const char* unit = nullptr;
    };
    const Metadata metadata[] = {
        {&FLAGS_timestamp_in_logfile_name, &logging_group},
        {&FLAGS_logtostderr, &logging_group},
        {&FLAGS_alsologtostderr, &logging_group},
        {&FLAGS_colorlogtostderr, &logging_group},
        {&FLAGS_colorlogtostdout, &logging_group},
        {&FLAGS_logtostdout, &logging_group},
#ifdef NGLOG_OS_LINUX
        {&FLAGS_drop_log_memory, &logging_group},
#endif
        {&FLAGS_stderrthreshold, &logging_group},
        {&FLAGS_alsologtoemail, &logging_group},
        {&FLAGS_log_file_header, &logging_group},
        {&FLAGS_log_prefix, &logging_group},
        {&FLAGS_log_year_in_prefix, &logging_group},
        {&FLAGS_minloglevel, &logging_group},
        {&FLAGS_logbuflevel, &logging_group},
        {&FLAGS_logbufsecs, &logging_group, nullptr, "s"},
        {&FLAGS_logcleansecs, &logging_group, nullptr, "s"},
        {&FLAGS_logemaillevel, &logging_group},
        {&FLAGS_logmailer, &logging_group},
        {&FLAGS_logfile_mode, &logging_group},
        {&FLAGS_log_dir, &logging_group},
        {&FLAGS_log_link, &logging_group},
        {&FLAGS_max_log_size, &logging_group, nullptr, "MiB"},
        {&FLAGS_stop_logging_if_full_disk, &logging_group},
        {&FLAGS_log_backtrace_at, &logging_group},
        {&FLAGS_log_utc_time, &logging_group},
        {&FLAGS_v, &verbose_logging_group},
        {&FLAGS_vmodule, &verbose_logging_group, "string[,string,...]"},
        {&FLAGS_symbolize_stacktrace, &stack_traces_group},
        {&FLAGS_symbolize_file_base_path, &stack_traces_group},
        {&FLAGS_symbolize_line_info, &stack_traces_group},
        {&FLAGS_symbolize_hyperlinks, &stack_traces_group},
        {&FLAGS_addr2line_timeout_ms, &stack_traces_group, nullptr, "ms"},
    };
    for (const Metadata& entry : metadata) {
      if (!entry.group->AddFlag(*entry.flag)) {
        std::abort();
      }
      nglog::internal::SetFlagDisplay(*entry.flag, entry.type, entry.unit);
    }
  }
};

const FlagMetadataInitializer kFlagMetadataInitializer;

}  // namespace
