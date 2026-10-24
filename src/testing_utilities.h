// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Test helpers that are not provided by GoogleTest itself: standard stream
// capture, environment overrides and names of parameterized test instances.

#ifndef NGLOG_SRC_TESTING_UTILITIES_H_
#define NGLOG_SRC_TESTING_UTILITIES_H_

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "config.h"
#include "ng-log/platform.h"
#include "stacktrace.h"
#include "utilities.h"

#if defined(NGLOG_OS_WINDOWS)
#  include "port.h"
#endif  // defined(NGLOG_OS_WINDOWS)

// Keeps the preceding call from becoming a tail call, so that a recursive test
// helper keeps one stack frame per level in optimized builds.
#if defined(__GNUC__)
#  define NGLOG_BLOCK_TAIL_CALL_OPTIMIZATION() __asm__("")
#elif defined(_MSC_VER)
#  include <intrin.h>
#  define NGLOG_BLOCK_TAIL_CALL_OPTIMIZATION() __nop()
#else
#  define NGLOG_BLOCK_TAIL_CALL_OPTIMIZATION() static_cast<void>(0)
#endif

namespace nglog {

extern int posix_strerror_r(int err, char* buf, std::size_t len);
extern std::string StrError(int err);

//! System temporary directory that other processes modify concurrently.
const std::string& SystemTmpDir();
//! Directory for temporary test files.
const std::string& TestTmpDir();

/**
 * Initializes logging with log files in the private temporary directory of
 * the test process. Concurrently running test processes of the same program
 * would otherwise create log files of the same name wherever process
 * identifiers repeat, such as on Emscripten, and report the collision on
 * stderr.
 */
void InitializeTestLogging(const char* program);

//! Sets an environment variable, or removes it if the value is null.
bool SetEnvironmentValue(const char* name, const char* value);

/**
 * Sets an environment variable, or removes it if the value is null, and
 * restores the previous state on destruction.
 */
class ScopedEnvironmentVariable final {
 public:
  ScopedEnvironmentVariable(const char* name, const char* value);
  ScopedEnvironmentVariable(const ScopedEnvironmentVariable& other) = delete;
  ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable& other) =
      delete;
  ~ScopedEnvironmentVariable();

 private:
  std::string name_;
  std::string previous_;
  bool had_previous_;
};

#ifdef GTEST_HAS_DEATH_TEST
//! Selects a death test style and restores the previous one.
class ScopedDeathTestStyle final {
 public:
  explicit ScopedDeathTestStyle(const char* style)
      : previous_(GTEST_FLAG_GET(death_test_style)) {
    GTEST_FLAG_SET(death_test_style, style);
  }
  ScopedDeathTestStyle(const ScopedDeathTestStyle& other) = delete;
  ScopedDeathTestStyle& operator=(const ScopedDeathTestStyle& other) = delete;
  ~ScopedDeathTestStyle() { GTEST_FLAG_SET(death_test_style, previous_); }

 private:
  std::string previous_;
};
#endif

//! Names each instance of a parameterized test after the name of its case.
struct CaseName {
  template <typename Case>
  std::string operator()(const testing::TestParamInfo<Case>& info) const {
    return info.param.name;
  }
};

//! Standard stream that a capture redirects.
enum class StandardStream { kStdout, kStderr };

/**
 * Redirects a standard stream into a private file while in scope.
 *
 * Captures nest, and the destructor restores the stream however the scope is
 * left, so a failing assertion cannot leave the stream redirected.
 */
class ScopedStreamCapture final {
 public:
  explicit ScopedStreamCapture(StandardStream stream);
  ScopedStreamCapture(const ScopedStreamCapture& other) = delete;
  ScopedStreamCapture& operator=(const ScopedStreamCapture& other) = delete;
  ~ScopedStreamCapture();

  //! Restores the stream and returns everything written to it while captured.
  std::string Stop();

  /**
   * Returns what reached the stream so far without flushing its buffer or
   * stopping the capture.
   */
  std::string Peek() const;

 private:
  void Restore();

  std::FILE* stream_;
  std::string filename_;
  FileDescriptor original_;
};

// Add hook for operator new to ensure there are no memory allocation.
extern void (*g_new_hook)();
// Receives the size of every allocation through operator new.
extern void (*g_new_size_hook)(std::size_t);

/**
 * Recursion depth of tests of a cut-off stack dump. It exceeds
 * kMaxStackDumpFrames by a margin, so that the dump is cut off even if the
 * unwinder misses a few of the recursive frames.
 */
constexpr int kDepthPastStackDumpLimit = kMaxStackDumpFrames + 8;

//! Result of CountFramesBeforeCutOffMarker() for output without the marker.
constexpr int kNoCutOffMarker = -1;

/**
 * Counts the frame lines that immediately precede the first line of `output`
 * that is kStackDumpCutOffMarker, or returns kNoCutOffMarker if there is no
 * such line.
 */
int CountFramesBeforeCutOffMarker(const std::string& output);

/**
 * Matches output with a stack dump that lists exactly `frames` frames and
 * ends with kStackDumpCutOffMarker.
 */
MATCHER_P(HasCutOffStackDump, frames,
          std::string{negation ? "has no" : "has a"} + " stack dump of " +
              testing::PrintToString(frames) +
              " frames that ends with the cut-off marker") {
  const int actual = CountFramesBeforeCutOffMarker(arg);
  if (actual == kNoCutOffMarker) {
    *result_listener << "which has no cut-off marker";
    return false;
  }
  *result_listener << "which has " << actual
                   << " frames before the cut-off marker";
  return actual == frames;
}

#if defined(HAVE_STACKTRACE)
// Whether the unwinder can walk more than kMaxStackDumpFrames frames. Some
// backends stop early, such as the x86 frame-pointer one in builds with
// -fomit-frame-pointer, so tests of a cut-off stack dump skip without it.
bool UnwinderWalksPastMaxStackDumpFrames();
#endif  // defined(HAVE_STACKTRACE)

}  // namespace nglog

#endif  // NGLOG_SRC_TESTING_UTILITIES_H_
