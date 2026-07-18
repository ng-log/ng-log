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
// Test helpers that are not provided by GoogleTest itself: golden-file
// stdout/stderr capture and diffing, a flag saver for non-gflags builds,
// and a setjmp-based ASSERT_DEATH that replaces GoogleTest's own
// (fork-based) implementation.

#ifndef NGLOG_SRC_TESTING_UTILITIES_H_
#define NGLOG_SRC_TESTING_UTILITIES_H_

#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "config.h"
#include "ng-log/logging.h"
#include "utilities.h"

#if defined(NGLOG_USE_WINDOWS_PORT)
#  include "port.h"
#endif  // defined(NGLOG_USE_WINDOWS_PORT)

namespace nglog {

extern void (*g_logging_fail_func)();
extern int posix_strerror_r(int err, char* buf, size_t len);
extern std::string StrError(int err);

// Directory for temporary test files, and the root of the source tree
// (used to locate golden files). Function-local statics rather than
// DECLARE_string/DEFINE_string flags: nothing needs these to be real
// command-line-settable gflags flags, and this sidesteps any
// cross-translation-unit extern-linkage or initialization-order concerns
// entirely.
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

// Save flags used from logging_unittest.cc.
#ifndef NGLOG_USE_GFLAGS
struct FlagSaver {
  FlagSaver()
      : v_(FLAGS_v),
        stderrthreshold_(FLAGS_stderrthreshold),
        logtostderr_(FLAGS_logtostderr),
        alsologtostderr_(FLAGS_alsologtostderr),
        logmailer_(FLAGS_logmailer) {}
  ~FlagSaver() {
    FLAGS_v = v_;
    FLAGS_stderrthreshold = stderrthreshold_;
    FLAGS_logtostderr = logtostderr_;
    FLAGS_alsologtostderr = alsologtostderr_;
    FLAGS_logmailer = logmailer_;
  }
  int v_;
  int stderrthreshold_;
  bool logtostderr_;
  bool alsologtostderr_;
  std::string logmailer_;
};
#endif  // !NGLOG_USE_GFLAGS

// Add hook for operator new to ensure there are no memory allocation.
extern void (*g_new_hook)();

extern bool g_called_abort;
extern std::jmp_buf g_jmp_buf;
void CalledAbort();

}  // namespace nglog

// Replace GoogleTest's own (fork-based) ASSERT_DEATH/ASSERT_DEBUG_DEATH with
// a setjmp-based implementation.
#ifdef ASSERT_DEATH
#  undef ASSERT_DEATH
#endif
#ifdef ASSERT_DEBUG_DEATH
#  undef ASSERT_DEBUG_DEATH
#endif

#ifdef NGLOG_OS_WINDOWS
// TODO(hamaji): Death test somehow doesn't work in Windows.
#  define ASSERT_DEATH(fn, msg)
#else
#  define ASSERT_DEATH(fn, msg)                                          \
    do {                                                                 \
      nglog::g_called_abort = false;                                     \
      /* in logging.cc */                                                \
      void (*original_logging_fail_func)() = nglog::g_logging_fail_func; \
      nglog::g_logging_fail_func = &nglog::CalledAbort;                  \
      if (!setjmp(nglog::g_jmp_buf)) fn;                                 \
      /* set back to their default */                                    \
      nglog::g_logging_fail_func = original_logging_fail_func;           \
      if (!nglog::g_called_abort) {                                      \
        fprintf(stderr, "Function didn't die (%s): %s\n", msg, #fn);     \
        exit(EXIT_FAILURE);                                              \
      }                                                                  \
    } while (0)
#endif

#ifdef NDEBUG
#  define ASSERT_DEBUG_DEATH(fn, msg)
#else
#  define ASSERT_DEBUG_DEATH(fn, msg) ASSERT_DEATH(fn, msg)
#endif  // NDEBUG

#endif  // NGLOG_SRC_TESTING_UTILITIES_H_
