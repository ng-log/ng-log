// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Logs from a static initializer and from a static destructor. The order
// relative to the ng-log library is unspecified, so the library must not rely
// on any of its own variables being dynamically initialized before or
// destroyed after these calls.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "testing_utilities.h"

namespace {

constexpr int kVerbosity = 1;

// Forwards to the logger it replaces and records its own destruction.
class RecordingLogger final : public nglog::base::Logger {
 public:
  explicit RecordingLogger(nglog::base::Logger* wrapped) noexcept
      : wrapped_{wrapped} {}
  ~RecordingLogger() override;

  void Write(bool force_flush,
             const std::chrono::system_clock::time_point& timestamp,
             const char* message, std::size_t message_len) override {
    wrapped_->Write(force_flush, timestamp, message, message_len);
  }
  void Flush() override { wrapped_->Flush(); }
  nglog::uint32 LogSize() override { return wrapped_->LogSize(); }

 private:
  nglog::base::Logger* wrapped_;
};

std::atomic<bool> g_logger_destroyed{false};

RecordingLogger::~RecordingLogger() { g_logger_destroyed = true; }

void Fail(const char* message) {
  std::fputs(message, stderr);
  std::fputc('\n', stderr);
  std::_Exit(EXIT_FAILURE);
}

struct StaticLogger {
  StaticLogger() {
    nglog::SetFlag(FLAGS_v, kVerbosity);
    LOG(INFO) << "static initializer";
    LOG(WARNING) << "static initializer";
    LOG(ERROR) << "static initializer";
    VLOG(kVerbosity) << "static initializer";
  }

  ~StaticLogger() {
    // The logger installed by main is released once main returns, so that its
    // destructor runs before the process terminates.
    if (!g_logger_destroyed) {
      Fail("the logger installed by main was not destroyed");
    }
    LOG(INFO) << "static destructor";
    LOG(WARNING) << "static destructor";
    LOG(ERROR) << "static destructor";
    VLOG(kVerbosity) << "static destructor";
    nglog::FlushLogFiles(NGLOG_INFO);
  }
};

StaticLogger static_logger;

}  // namespace

int main(int /*argc*/, char** argv) {
  nglog::SetFlag(FLAGS_log_dir, nglog::TestTmpDir());
  nglog::InitializeLogging(argv[0]);
  nglog::base::SetLogger(
      NGLOG_INFO, new RecordingLogger{nglog::base::GetLogger(NGLOG_INFO)});
  LOG(INFO) << "main";
  return EXIT_SUCCESS;
}
