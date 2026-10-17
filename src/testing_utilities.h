// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Test helpers that are not provided by GoogleTest itself: standard stream
// capture, golden file comparison, environment overrides and names of
// parameterized test instances.

#ifndef NGLOG_SRC_TESTING_UTILITIES_H_
#define NGLOG_SRC_TESTING_UTILITIES_H_

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include "config.h"
#include "ng-log/logging.h"
#include "utilities.h"

#if defined(NGLOG_OS_WINDOWS)
#  include "port.h"
#endif  // defined(NGLOG_OS_WINDOWS)

namespace nglog {

extern int posix_strerror_r(int err, char* buf, size_t len);
extern std::string StrError(int err);

//! System temporary directory that other processes modify concurrently.
const std::string& SystemTmpDir();
// Directory for temporary test files and the root of the source tree.
const std::string& TestTmpDir();
const std::string& TestSrcDir();

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

//! Names each instance of a parameterized test after the name of its case.
struct CaseName {
  template <typename Case>
  std::string operator()(const testing::TestParamInfo<Case>& info) const {
    return info.param.name;
  }
};

// Placeholder pointer value logged by tests so that Munge() can normalize
// it to a fixed golden-file placeholder regardless of the actual pointer
// value produced at runtime.
constexpr std::uint32_t kPtrTestValue = 0x12345678;

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

/**
 * Checks that output equals the contents of a golden file once dates, times,
 * line numbers, thread identifiers, pointers and error descriptions are
 * normalized. A mismatch is explained by a diff of the lines around each
 * change.
 */
testing::AssertionResult MatchesGoldenFile(const std::string& output,
                                           const std::string& golden_filename);

// Add hook for operator new to ensure there are no memory allocation.
extern void (*g_new_hook)();

}  // namespace nglog

#endif  // NGLOG_SRC_TESTING_UTILITIES_H_
