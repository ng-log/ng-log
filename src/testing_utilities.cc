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

#include "testing_utilities.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <sstream>
#include <utility>
#include <vector>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

namespace nglog {
extern void GetExistingTempDirectories(std::vector<std::string>& list);
}  // namespace nglog

namespace {

std::string GetTempDir() {
  std::vector<std::string> temp_directories_list;
  nglog::GetExistingTempDirectories(temp_directories_list);

  if (temp_directories_list.empty()) {
    fprintf(stderr, "No temporary directory found\n");
    exit(EXIT_FAILURE);
  }

  // Use first directory from list of existing temporary directories.
  return temp_directories_list.front();
}

}  // namespace

#if defined(NGLOG_OS_WINDOWS) && defined(_MSC_VER) && !defined(TEST_SRC_DIR)
// The test will run in glog/vsproject/<project name>
// (e.g., glog/vsproject/logging_unittest).
static const char TEST_SRC_DIR[] = "../..";
#elif !defined(TEST_SRC_DIR)
#  warning TEST_SRC_DIR should be defined in config.h
static const char TEST_SRC_DIR[] = ".";
#endif

namespace nglog {

const std::string& TestTmpDir() {
  static const std::string dir = GetTempDir();
  return dir;
}

const std::string& TestSrcDir() {
  static const std::string dir = TEST_SRC_DIR;
  return dir;
}

CapturedStream::CapturedStream(int fd, std::string filename)
    : fd_(fd), filename_(std::move(filename)) {
  Capture();
}

void CapturedStream::Capture() {
  // Keep original stream for later
  CHECK(!uncaptured_fd_) << ", Stream " << fd_ << " already captured!";
  uncaptured_fd_.reset(dup(fd_));
  CHECK(uncaptured_fd_);

  // Open file to save stream to
  FileDescriptor cap_fd{
      open(filename_.c_str(), O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR)};
  CHECK(cap_fd);

  // Send stdout/stderr to this file
  fflush(nullptr);
  CHECK(dup2(cap_fd.get(), fd_) != -1);
  CHECK(cap_fd.close() != -1);
}

void CapturedStream::StopCapture() {
  // Restore original stream
  if (uncaptured_fd_) {
    fflush(nullptr);
    CHECK(dup2(uncaptured_fd_.get(), fd_) != -1);
  }
}

namespace {

std::map<int, std::unique_ptr<CapturedStream>> s_captured_streams;

// Redirect a file descriptor to a file.
//   fd       - Should be stdout or stderr
//   filename - File where output should be stored
void CaptureTestOutput(int fd, const std::string& filename) {
  CHECK((fd == fileno(stdout)) || (fd == fileno(stderr)));
  CHECK(s_captured_streams.find(fd) == s_captured_streams.end());
  s_captured_streams[fd] = std::make_unique<CapturedStream>(fd, filename);
}

// Return the size (in bytes) of a file
size_t GetFileSize(FILE* file) {
  fseek(file, 0, SEEK_END);
  return static_cast<size_t>(ftell(file));
}

std::vector<std::string> SplitLines(const std::string& s) {
  std::vector<std::string> lines;
  std::string::size_type start = 0;
  while (start < s.size()) {
    const std::string::size_type pos = s.find('\n', start);
    if (pos == std::string::npos) {
      lines.push_back(s.substr(start));
      break;
    }
    lines.push_back(s.substr(start, pos - start));
    start = pos + 1;
  }
  return lines;
}

// Print a minimal unified-style diff of two line sequences directly to
// stderr. Deliberately done in-process rather than via temp files and an
// external diff/fc tool: those depend on platform-specific quirks (working
// directory, path separators, sandboxing) that have made the diff itself
// fail to show on some CI platforms, hiding the actual mismatch.
void PrintDiff(const std::vector<std::string>& golden,
               const std::vector<std::string>& actual) {
  const size_t rows = golden.size();
  const size_t cols = actual.size();

  // lcs[i][j] holds the length of the longest common subsequence of
  // golden[i:] and actual[j:].
  std::vector<std::vector<int>> lcs(rows + 1, std::vector<int>(cols + 1, 0));
  for (size_t i = rows; i-- > 0;) {
    for (size_t j = cols; j-- > 0;) {
      lcs[i][j] = golden[i] == actual[j]
                      ? lcs[i + 1][j + 1] + 1
                      : std::max(lcs[i + 1][j], lcs[i][j + 1]);
    }
  }

  size_t i = 0;
  size_t j = 0;
  while (i < rows && j < cols) {
    if (golden[i] == actual[j]) {
      fprintf(stderr, "  %s\n", golden[i].c_str());
      ++i;
      ++j;
    } else if (lcs[i + 1][j] >= lcs[i][j + 1]) {
      fprintf(stderr, "- %s\n", golden[i].c_str());
      ++i;
    } else {
      fprintf(stderr, "+ %s\n", actual[j].c_str());
      ++j;
    }
  }
  for (; i < rows; ++i) {
    fprintf(stderr, "- %s\n", golden[i].c_str());
  }
  for (; j < cols; ++j) {
    fprintf(stderr, "+ %s\n", actual[j].c_str());
  }
}

// Get the captured stdout or stderr as a string
std::string GetCapturedTestOutput(int fd) {
  CHECK((fd == fileno(stdout)) || (fd == fileno(stderr)));
  auto pos = s_captured_streams.find(fd);
  CHECK(pos != s_captured_streams.end())
      << ": did you forget CaptureTestStdout() or CaptureTestStderr()?";
  std::unique_ptr<CapturedStream> cap = std::move(pos->second);
  s_captured_streams.erase(pos);

  // Make sure everything is flushed.
  cap->StopCapture();

  // Read the captured file.
  std::unique_ptr<FILE> file{fopen(cap->filename().c_str(), "r")};
  const std::string content = ReadEntireFile(file.get());
  file.reset();

  return content;
}

constexpr std::size_t kLoggingPrefixLength = 9;

// Check if the string is [IWEF](\d{8}|YEARDATE)
bool IsLoggingPrefix(const std::string& s) {
  if (s.size() != kLoggingPrefixLength) {
    return false;
  }
  if (!strchr("IWEF", s[0])) return false;
  for (size_t i = 1; i <= 8; ++i) {
    if (!isdigit(s[i]) && s[i] != "YEARDATE"[i - 1]) return false;
  }
  return true;
}

// Convert log output into normalized form.
//
// Example:
//     I20200102 030405 logging_unittest.cc:345] RAW: vlog -1
//  => IYEARDATE TIME__ logging_unittest.cc:LINE] RAW: vlog -1
std::string MungeLine(const std::string& line) {
  std::string before, logcode_date, time, thread_lineinfo;
  std::size_t begin_of_logging_prefix = 0;
  for (; begin_of_logging_prefix + kLoggingPrefixLength < line.size();
       ++begin_of_logging_prefix) {
    if (IsLoggingPrefix(
            line.substr(begin_of_logging_prefix, kLoggingPrefixLength))) {
      break;
    }
  }
  if (begin_of_logging_prefix + kLoggingPrefixLength >= line.size()) {
    return line;
  } else if (begin_of_logging_prefix > 0) {
    before = line.substr(0, begin_of_logging_prefix - 1);
  }
  std::istringstream iss(line.substr(begin_of_logging_prefix));
  iss >> logcode_date;
  iss >> time;
  iss >> thread_lineinfo;
  CHECK(!thread_lineinfo.empty());
  if (thread_lineinfo[thread_lineinfo.size() - 1] != ']') {
    // We found thread ID.
    std::string tmp;
    iss >> tmp;
    CHECK(!tmp.empty());
    CHECK_EQ(']', tmp[tmp.size() - 1]);
    thread_lineinfo = "THREADID " + tmp;
  }
  size_t index = thread_lineinfo.find(':');
  CHECK_NE(std::string::npos, index);
  thread_lineinfo = thread_lineinfo.substr(0, index + 1) + "LINE]";
  std::string rest;
  std::getline(iss, rest);
  return (before + logcode_date[0] + "YEARDATE TIME__ " + thread_lineinfo +
          MungeLine(rest));
}

void StringReplace(std::string* str, const std::string& oldsub,
                   const std::string& newsub) {
  size_t pos = str->find(oldsub);
  if (pos != std::string::npos) {
    str->replace(pos, oldsub.size(), newsub);
  }
}

std::string Munge(const std::string& filename) {
  std::unique_ptr<FILE> fp{fopen(filename.c_str(), "rb")};
  CHECK(fp != nullptr) << filename << ": couldn't open";
  char buf[4096];
  std::string result;
  while (fgets(buf, 4095, fp.get())) {
    std::string line = MungeLine(buf);
    const size_t str_size = 256;
    char null_str[str_size];
    char ptr_str[str_size];
    std::snprintf(null_str, str_size, "%p", static_cast<void*>(nullptr));
    std::snprintf(ptr_str, str_size, "%p",
                  reinterpret_cast<void*>(kPtrTestValue));

    StringReplace(&line, "__NULLP__", null_str);
    StringReplace(&line, "__PTRTEST__", ptr_str);

    StringReplace(&line, "__SUCCESS__", StrError(0));
    StringReplace(&line, "__ENOENT__", StrError(ENOENT));
    StringReplace(&line, "__EINTR__", StrError(EINTR));
    StringReplace(&line, "__ENXIO__", StrError(ENXIO));
    StringReplace(&line, "__ENOEXEC__", StrError(ENOEXEC));
    result += line + "\n";
  }
  return result;
}

bool MungeAndDiffTest(const std::string& golden_filename, CapturedStream* cap) {
  cap->StopCapture();

  // Run munge
  const std::string captured = Munge(cap->filename());
  const std::string golden = Munge(golden_filename);
  if (captured != golden) {
    fprintf(stderr,
            "Test with golden file failed. Diff (- golden, + actual):\n");
    PrintDiff(SplitLines(golden), SplitLines(captured));
    return false;
  }
  LOG(INFO) << "Diff was successful";
  return true;
}

}  // namespace

// Read the entire content of a file as a string
std::string ReadEntireFile(FILE* file) {
  const size_t file_size = GetFileSize(file);
  std::vector<char> content(file_size);

  size_t bytes_last_read = 0;  // # of bytes read in the last fread()
  size_t bytes_read = 0;       // # of bytes read so far

  fseek(file, 0, SEEK_SET);

  // Keep reading the file until we cannot read further or the
  // pre-determined file size is reached.
  do {
    bytes_last_read =
        fread(content.data() + bytes_read, 1, file_size - bytes_read, file);
    bytes_read += bytes_last_read;
  } while (bytes_last_read > 0 && bytes_read < file_size);

  return std::string(content.data(), bytes_read);
}

void CaptureTestStdout() {
  CaptureTestOutput(fileno(stdout), TestTmpDir() + "/captured.out");
}

void CaptureTestStderr() {
  CaptureTestOutput(fileno(stderr), TestTmpDir() + "/captured.err");
}

std::string GetCapturedTestStdout() {
  return GetCapturedTestOutput(fileno(stdout));
}

std::string GetCapturedTestStderr() {
  return GetCapturedTestOutput(fileno(stderr));
}

bool MungeAndDiffTestStdout(const std::string& golden_filename) {
  auto pos = s_captured_streams.find(fileno(stdout));
  CHECK(pos != s_captured_streams.end())
      << ": did you forget CaptureTestStdout()?";
  return MungeAndDiffTest(golden_filename, pos->second.get());
}

bool MungeAndDiffTestStderr(const std::string& golden_filename) {
  auto pos = s_captured_streams.find(fileno(stderr));
  CHECK(pos != s_captured_streams.end())
      << ": did you forget CaptureTestStderr()?";
  return MungeAndDiffTest(golden_filename, pos->second.get());
}

void (*g_new_hook)() = nullptr;

bool g_called_abort;
std::jmp_buf g_jmp_buf;

void CalledAbort() {
  g_called_abort = true;
  longjmp(g_jmp_buf, 1);
}

}  // namespace nglog

// noinline prevents a -Wmismatched-new-delete false positive: GCC loses
// track of the malloc/free pairing across inlined operator new/delete.
// See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=103993
[[gnu::noinline]] void* operator new(size_t size,
                                     const std::nothrow_t&) noexcept {
  if (nglog::g_new_hook) {
    nglog::g_new_hook();
  }
  return malloc(size);
}

[[gnu::noinline]] void* operator new(size_t size) {
  if (nglog::g_new_hook) {
    nglog::g_new_hook();
  }
  void* p = malloc(size);
  if (p == nullptr) {
    throw std::bad_alloc{};
  }
  return p;
}

[[gnu::noinline]] void* operator new[](size_t size) {
  if (nglog::g_new_hook) {
    nglog::g_new_hook();
  }
  void* p = malloc(size);
  if (p == nullptr) {
    throw std::bad_alloc{};
  }
  return p;
}

[[gnu::noinline]] void operator delete(void* p) noexcept { free(p); }

[[gnu::noinline]] void operator delete(void* p, size_t) noexcept { free(p); }

[[gnu::noinline]] void operator delete[](void* p) noexcept { free(p); }

[[gnu::noinline]] void operator delete[](void* p, size_t) noexcept { free(p); }
