// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Implements the setup functions of the experimental logging functions on top
// of those of the logging macros.

#include "ng-log/experimental/setup.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "config.h"
#include "context.h"
#include "internal/log_setup.h"
#include "ng-log/experimental/attributes.h"
#include "ng-log/experimental/sink.h"
#include "ng-log/flags.h"
#include "ng-log/log_setup.h"
#include "ng-log/log_sink.h"
#include "ng-log/platform.h"
#include "ng-log/raw_logging.h"
#include "utilities.h"

namespace nglog::experimental {
namespace {

// Terminates after reporting a misuse like a failed check.
[[noreturn]] void Fail(const std::string& message) {
  RAW_LOG(FATAL, "Check failed: %s", message.c_str());
  // A fatal raw message terminates unless a failure function returns.
  std::abort();
}

// Names of files and of the mailer's addresses end at a null character once
// they reach the system.
void EnsureNoNullCharacter(std::string_view value, const char* what) {
  if (const std::size_t index = value.find('\0');
      index != std::string_view::npos) {
    Fail(std::format(
        "the {} must not contain a null character, but the character at "
        "index {} of {} is one",
        what, index, value.size()));
  }
}

// The narrow name the logging macros take for a path, which is UTF-8 on
// Windows.
std::string NarrowName(const std::filesystem::path& path) {
#if defined(NGLOG_OS_WINDOWS)
  const std::u8string name = path.u8string();
  return std::string{name.begin(), name.end()};
#else
  return path.native();
#endif
}

// Writes the failure dump through the writer SetFailureWriter() installed.
// Called from signal handlers, so the writer must be read without a lock.
std::atomic<FailureWriter> failure_writer{nullptr};

static_assert(std::atomic<FailureWriter>::is_always_lock_free,
              "The failure signal handler reads the failure writer through an "
              "atomic function pointer, which is not lock-free on this "
              "platform. Disable the experimental logging functions with "
              "WITH_EXPERIMENTAL=OFF.");

void WriteFailure(const char* data, std::size_t size) {
  failure_writer.load(std::memory_order_relaxed)(std::string_view{data, size});
}

// Forwards the messages of the experimental logging functions to a structured
// sink. Messages of the macros carry no source location, which records
// require, and are therefore skipped.
class RegisteredSink final : public LogSink {
 public:
  explicit RegisteredSink(Sink& sink) noexcept : sink_{&sink} {}

  void send(LogSeverity severity, const char* /*full_filename*/,
            const char* /*base_filename*/, int /*line*/,
            const LogMessageTime& time, const char* message,
            std::size_t message_len) override {
    if (const internal::ContextImpl* const context =
            internal::SendingContext()) {
      const std::array sinks{sink_};
      internal::SendRecord(*context, sinks, severity, time,
                           std::string_view{message, message_len});
    }
  }

  const Sink* sink() const noexcept { return sink_; }

 private:
  Sink* sink_;
};

// Structured sinks AddSink() registered.
class SinkRegistry {
 public:
  static SinkRegistry& Instance() {
    // Never destroyed, since messages logged during static destruction may
    // still reach the sinks.
    static SinkRegistry* const registry = new SinkRegistry;
    return *registry;
  }

  void Add(Sink& sink) {
    const std::scoped_lock lock{mutex_};
    if (Find(sink) != sinks_.end()) {
      Fail("the sink was added already");
    }
    sinks_.push_back(std::make_unique<RegisteredSink>(sink));
    AddLogSink(sinks_.back().get());
  }

  void Remove(Sink& sink) {
    const std::scoped_lock lock{mutex_};
    const auto it = Find(sink);
    if (it == sinks_.end()) {
      Fail("the sink was not added");
    }
    // Waits until no other thread sends a message to the sink.
    RemoveLogSink(it->get());
    sinks_.erase(it);
  }

 private:
  std::vector<std::unique_ptr<RegisteredSink>>::iterator Find(
      const Sink& sink) {
    return std::ranges::find(sinks_, &sink, &RegisteredSink::sink);
  }

  std::mutex mutex_;
  std::vector<std::unique_ptr<RegisteredSink>> sinks_;
};

}  // namespace

void Initialize(std::string_view program_name) {
  EnsureNoNullCharacter(program_name, "program name");
  nglog::internal::InitializeLogging(program_name.data(), program_name.size());
}

bool IsInitialized() noexcept { return IsLoggingInitialized(); }

void Shutdown() { ShutdownLogging(); }

void SetVerbosity(int level) {
  SetFlag(FLAGS_v, static_cast<std::int32_t>(level));
}

int SetVerbosity(std::string_view module_pattern, int level) {
  return nglog::internal::SetVLOGLevel(module_pattern.data(),
                                       module_pattern.size(), level);
}

void SetStderrThreshold(Severity severity) { SetStderrLogging(severity.value); }

void LogOnlyToStderr() { LogToStderr(); }

void SetLogFile(Severity severity, const std::filesystem::path& base_filename) {
  const std::string name = NarrowName(base_filename);
  if (name.empty()) {
    Fail(
        "the base file name must not be empty. DisableLogFile() stops writing "
        "a log file");
  }
  EnsureNoNullCharacter(name, "base file name");
  nglog::internal::SetLogDestination(severity.value, name.data(), name.size());
}

void DisableLogFile(Severity severity) {
  // An empty name stops writing the log file.
  nglog::internal::SetLogDestination(severity.value, "", 0);
}

void SetLogFileLink(Severity severity, const std::filesystem::path& link_name) {
  const std::string name = NarrowName(link_name);
  EnsureNoNullCharacter(name, "link name");
  if (name.empty() || link_name.has_parent_path()) {
    Fail(std::format(
        "the link name must be a file name without directories, but it is "
        "'{}'",
        name));
  }
  nglog::internal::SetLogSymlink(severity.value, name.data(), name.size());
}

void DisableLogFileLink(Severity severity) {
  // An empty name stops linking to the latest log file.
  nglog::internal::SetLogSymlink(severity.value, "", 0);
}

void SetLogFileSuffix(std::string_view suffix) {
  EnsureNoNullCharacter(suffix, "log file suffix");
  nglog::internal::SetLogFilenameExtension(suffix.data(), suffix.size());
}

void SetEmailThreshold(Severity severity, std::string_view addresses) {
  EnsureNoNullCharacter(addresses, "email addresses");
  nglog::internal::SetEmailLogging(severity.value, addresses.data(),
                                   addresses.size());
}

void Flush(Severity severity) { FlushLogFiles(severity.value); }

std::string_view Name(Severity severity) noexcept {
  return GetLogSeverityName(severity.value);
}

std::vector<std::filesystem::path> LogDirectories() {
  std::vector<std::filesystem::path> directories;
  for (const std::string& directory : GetLoggingDirectories()) {
#if defined(NGLOG_OS_WINDOWS)
    directories.emplace_back(std::u8string{directory.begin(), directory.end()});
#else
    directories.emplace_back(directory);
#endif
  }
  return directories;
}

void SetFingerprint(std::string_view fingerprint) {
  SetApplicationFingerprint(std::string{fingerprint});
}

FatalHandler SetFatalHandler(FatalHandler handler) {
  if (handler == nullptr) {
    Fail("the fatal handler must not be null");
  }
  return InstallFailureFunction(handler);
}

void SetFailureWriter(FailureWriter writer) {
  if (writer == nullptr) {
    Fail("the failure writer must not be null");
  }
  failure_writer.store(writer, std::memory_order_relaxed);
  InstallFailureWriter(&WriteFailure);
}

void AddSink(Sink& sink) { SinkRegistry::Instance().Add(sink); }

void RemoveSink(Sink& sink) { SinkRegistry::Instance().Remove(sink); }

std::string StackTrace() {
#if defined(HAVE_STACKTRACE)
  std::string trace;
  // Skips this function like GetStackTrace() skips itself.
  DumpStackTrace(1, DebugWriteToString, &trace);
  return trace;
#else
  Fail("stack traces are not supported on this platform");
#endif
}

}  // namespace nglog::experimental
