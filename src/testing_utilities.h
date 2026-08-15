// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Test helpers that are not provided by GoogleTest itself: golden-file
// stdout/stderr capture and diffing.

#ifndef NGLOG_SRC_TESTING_UTILITIES_H_
#define NGLOG_SRC_TESTING_UTILITIES_H_

#include <cstdint>
#include <string>

#include "config.h"
#include "ng-log/logging.h"
#include "utilities.h"

#if defined(NGLOG_OS_WINDOWS)
#  include "port.h"
#endif  // defined(NGLOG_OS_WINDOWS)

namespace nglog {

extern void (*g_logging_fail_func)();
extern int posix_strerror_r(int err, char* buf, size_t len);
extern std::string StrError(int err);

// Directory for temporary test files and the root of the source tree.
const std::string& TestTmpDir();
const std::string& TestSrcDir();

// Placeholder pointer value logged by tests so that Munge() can normalize
// it to a fixed golden-file placeholder regardless of the actual pointer
// value produced at runtime.
constexpr std::uint32_t kPtrTestValue = 0x12345678;

// ----------------------------------------------------------------------
// Golden file functions
// ----------------------------------------------------------------------

// Read the entire content of a file as a string.
std::string ReadEntireFile(FILE* file);

class CapturedStream {
 public:
  CapturedStream(int fd, std::string filename);

  // Start redirecting output to a file
  void Capture();

  // Remove output redirection
  void StopCapture();

  const std::string& filename() const { return filename_; }

 private:
  int fd_;                        // file descriptor being captured
  FileDescriptor uncaptured_fd_;  // where the stream was originally sent to
  std::string filename_;          // file where stream is being saved
};

// Redirect stdout/stderr to a file so it can later be compared against a
// golden file.
void CaptureTestStdout();
void CaptureTestStderr();

// Return the captured stdout/stderr of a test as a string. Stops the
// capture in the process.
std::string GetCapturedTestStdout();
std::string GetCapturedTestStderr();

// Compare the captured stdout/stderr, after normalization, against the
// given golden file.
bool MungeAndDiffTestStdout(const std::string& golden_filename);
bool MungeAndDiffTestStderr(const std::string& golden_filename);

// Add hook for operator new to ensure there are no memory allocation.
extern void (*g_new_hook)();

}  // namespace nglog

#endif  // NGLOG_SRC_TESTING_UTILITIES_H_
