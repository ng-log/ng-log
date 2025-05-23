// Copyright (c) 2024, Google Inc.
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
// Author: Shinichiro Hamaji
//   (based on googletest: http://code.google.com/p/googletest/)

#ifdef GOOGLETEST_H__
#  error You must not include this file twice.
#endif
#define GOOGLETEST_H__

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cctype>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <new>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "config.h"
#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#if defined(NGLOG_USE_WINDOWS_PORT)
#  include "port.h"
#endif  // defined(NGLOG_USE_WINDOWS_PORT)
#include "base/commandlineflags.h"
#include "ng-log/logging.h"
#include "utilities.h"

#if __cplusplus < 201103L && !defined(_MSC_VER)
#  define NGLOG_GLOG_THROW_BAD_ALLOC throw(std::bad_alloc)
#else
#  define NGLOG_GLOG_THROW_BAD_ALLOC
#endif

using std::map;
using std::string;
using std::vector;

namespace nglog {
extern void (*g_logging_fail_func)();
extern void GetExistingTempDirectories(std::vector<std::string>& list);
extern int posix_strerror_r(int err, char* buf, size_t len);
extern std::string StrError(int err);
}  // namespace nglog

#undef NGLOG_EXPORT
#define NGLOG_EXPORT

static inline string GetTempDir() {
  vector<string> temp_directories_list;
  nglog::GetExistingTempDirectories(temp_directories_list);

  if (temp_directories_list.empty()) {
    fprintf(stderr, "No temporary directory found\n");
    exit(EXIT_FAILURE);
  }

  // Use first directory from list of existing temporary directories.
  return temp_directories_list.front();
}

#if defined(NGLOG_OS_WINDOWS) && defined(_MSC_VER) && !defined(TEST_SRC_DIR)
// The test will run in glog/vsproject/<project name>
// (e.g., glog/vsproject/logging_unittest).
static const char TEST_SRC_DIR[] = "../..";
#elif !defined(TEST_SRC_DIR)
#  warning TEST_SRC_DIR should be defined in config.h
static const char TEST_SRC_DIR[] = ".";
#endif

static const uint32_t PTR_TEST_VALUE = 0x12345678;

DEFINE_string(test_tmpdir, GetTempDir(), "Dir we use for temp files");
DEFINE_string(test_srcdir, TEST_SRC_DIR,
              "Source-dir root, needed to find nglog_unittest_flagfile");
DEFINE_bool(run_benchmark, false, "If true, run benchmarks");
#ifdef NDEBUG
DEFINE_int32(benchmark_iters, 100000000, "Number of iterations per benchmark");
#else
DEFINE_int32(benchmark_iters, 100000, "Number of iterations per benchmark");
#endif

#ifdef HAVE_LIB_GTEST
#  include <gtest/gtest.h>
// Use our ASSERT_DEATH implementation.
#  undef ASSERT_DEATH
#  undef ASSERT_DEBUG_DEATH
using testing::InitGoogleTest;
#else

namespace nglog {

void InitGoogleTest(int*, char**);

void InitGoogleTest(int*, char**) {}

// The following is some bare-bones testing infrastructure

#  define EXPECT_NEAR(val1, val2, abs_error)                         \
    do {                                                             \
      if (abs(val1 - val2) > abs_error) {                            \
        fprintf(stderr, "Check failed: %s within %s of %s\n", #val1, \
                #abs_error, #val2);                                  \
        exit(EXIT_FAILURE);                                          \
      }                                                              \
    } while (0)

#  define EXPECT_TRUE(cond)                           \
    do {                                              \
      if (!(cond)) {                                  \
        fprintf(stderr, "Check failed: %s\n", #cond); \
        exit(EXIT_FAILURE);                           \
      }                                               \
    } while (0)

#  define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))

#  define EXPECT_OP(op, val1, val2)                                     \
    do {                                                                \
      if (!((val1)op(val2))) {                                          \
        fprintf(stderr, "Check failed: %s %s %s\n", #val1, #op, #val2); \
        exit(EXIT_FAILURE);                                             \
      }                                                                 \
    } while (0)

#  define EXPECT_EQ(val1, val2) EXPECT_OP(==, val1, val2)
#  define EXPECT_NE(val1, val2) EXPECT_OP(!=, val1, val2)
#  define EXPECT_GT(val1, val2) EXPECT_OP(>, val1, val2)
#  define EXPECT_LT(val1, val2) EXPECT_OP(<, val1, val2)

#  define EXPECT_NAN(arg)                                   \
    do {                                                    \
      if (!isnan(arg)) {                                    \
        fprintf(stderr, "Check failed: isnan(%s)\n", #arg); \
        exit(EXIT_FAILURE);                                 \
      }                                                     \
    } while (0)

#  define EXPECT_INF(arg)                                   \
    do {                                                    \
      if (!isinf(arg)) {                                    \
        fprintf(stderr, "Check failed: isinf(%s)\n", #arg); \
        exit(EXIT_FAILURE);                                 \
      }                                                     \
    } while (0)

#  define EXPECT_DOUBLE_EQ(val1, val2)                             \
    do {                                                           \
      if (((val1) < (val2) - 0.001 || (val1) > (val2) + 0.001)) {  \
        fprintf(stderr, "Check failed: %s == %s\n", #val1, #val2); \
        exit(EXIT_FAILURE);                                        \
      }                                                            \
    } while (0)

#  define EXPECT_STREQ(val1, val2)                                      \
    do {                                                                \
      if (strcmp((val1), (val2)) != 0) {                                \
        fprintf(stderr, "Check failed: streq(%s, %s)\n", #val1, #val2); \
        exit(EXIT_FAILURE);                                             \
      }                                                                 \
    } while (0)

#  define EXPECT_THROW(statement, exception)                    \
    do {                                                        \
      try {                                                     \
        statement;                                              \
      } catch (const exception&) {                              \
        printf("ok\n");                                         \
      } catch (...) {                                           \
        fprintf(stderr, "%s\n", "Unexpected exception thrown"); \
        exit(EXIT_FAILURE);                                     \
      }                                                         \
    } while (0)

vector<void (*)()> g_testlist;  // the tests to run
#  define TEST(a, b)                                   \
    struct Test_##a##_##b {                            \
      Test_##a##_##b() { g_testlist.push_back(&Run); } \
      static void Run() {                              \
        FlagSaver fs;                                  \
        RunTest();                                     \
      }                                                \
      static void RunTest();                           \
    };                                                 \
    static Test_##a##_##b g_test_##a##_##b;            \
    void Test_##a##_##b::RunTest()

static inline int RUN_ALL_TESTS() {
  vector<void (*)()>::const_iterator it;
  for (it = g_testlist.begin(); it != g_testlist.end(); ++it) {
    (*it)();
  }
  fprintf(stderr, "Passed %d tests\n\nPASS\n",
          static_cast<int>(g_testlist.size()));
  return 0;
}

}  // namespace nglog

#endif  // ! HAVE_LIB_GTEST

namespace nglog {

static bool g_called_abort;
static jmp_buf g_jmp_buf;
static inline void CalledAbort() {
  g_called_abort = true;
  longjmp(g_jmp_buf, 1);
}

#ifdef NGLOG_OS_WINDOWS
// TODO(hamaji): Death test somehow doesn't work in Windows.
#  define ASSERT_DEATH(fn, msg)
#else
#  define ASSERT_DEATH(fn, msg)                                      \
    do {                                                             \
      g_called_abort = false;                                        \
      /* in logging.cc */                                            \
      void (*original_logging_fail_func)() = g_logging_fail_func;    \
      g_logging_fail_func = &CalledAbort;                            \
      if (!setjmp(g_jmp_buf)) fn;                                    \
      /* set back to their default */                                \
      g_logging_fail_func = original_logging_fail_func;              \
      if (!g_called_abort) {                                         \
        fprintf(stderr, "Function didn't die (%s): %s\n", msg, #fn); \
        exit(EXIT_FAILURE);                                          \
      }                                                              \
    } while (0)
#endif

#ifdef NDEBUG
#  define ASSERT_DEBUG_DEATH(fn, msg)
#else
#  define ASSERT_DEBUG_DEATH(fn, msg) ASSERT_DEATH(fn, msg)
#endif  // NDEBUG

// Benchmark tools.

#define BENCHMARK(n) static BenchmarkRegisterer __benchmark_##n(#n, &n);

map<string, void (*)(int)> g_benchlist;  // the benchmarks to run

class BenchmarkRegisterer {
 public:
  BenchmarkRegisterer(const char* name, void (*function)(int iters)) {
    EXPECT_TRUE(g_benchlist.insert(std::make_pair(name, function)).second);
  }
};

static inline void RunSpecifiedBenchmarks() {
  if (!FLAGS_run_benchmark) {
    return;
  }

  int iter_cnt = FLAGS_benchmark_iters;
  puts("Benchmark\tTime(ns)\tIterations");
  for (auto& iter : g_benchlist) {
    clock_t start = clock();
    iter.second(iter_cnt);
    double elapsed_ns = (static_cast<double>(clock()) - start) /
                        CLOCKS_PER_SEC * 1000 * 1000 * 1000;
#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wformat="
#endif
    printf("%s\t%8.2lf\t%10d\n", iter.first.c_str(), elapsed_ns / iter_cnt,
           iter_cnt);
#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic pop
#endif
  }
  puts("");
}

// ----------------------------------------------------------------------
// Golden file functions
// ----------------------------------------------------------------------

class CapturedStream {
 public:
  CapturedStream(int fd, string filename)
      : fd_(fd), filename_(std::move(filename)) {
    Capture();
  }

  // Start redirecting output to a file
  void Capture() {
    // Keep original stream for later
    CHECK(!uncaptured_fd_) << ", Stream " << fd_ << " already captured!";
    uncaptured_fd_.reset(dup(fd_));
    CHECK(uncaptured_fd_);

    // Open file to save stream to
    FileDescriptor cap_fd{open(filename_.c_str(), O_CREAT | O_TRUNC | O_WRONLY,
                               S_IRUSR | S_IWUSR)};
    CHECK(cap_fd);

    // Send stdout/stderr to this file
    fflush(nullptr);
    CHECK(dup2(cap_fd.get(), fd_) != -1);
    CHECK(cap_fd.close() != -1);
  }

  // Remove output redirection
  void StopCapture() {
    // Restore original stream
    if (uncaptured_fd_) {
      fflush(nullptr);
      CHECK(dup2(uncaptured_fd_.get(), fd_) != -1);
    }
  }

  const string& filename() const { return filename_; }

 private:
  int fd_;  // file descriptor being captured
  FileDescriptor
      uncaptured_fd_;  // where the stream was originally being sent to
  string filename_;    // file where stream is being saved
};
static std::map<int, std::unique_ptr<CapturedStream>> s_captured_streams;
// Redirect a file descriptor to a file.
//   fd       - Should be stdout or stderr
//   filename - File where output should be stored
static inline void CaptureTestOutput(int fd, const string& filename) {
  CHECK((fd == fileno(stdout)) || (fd == fileno(stderr)));
  CHECK(s_captured_streams.find(fd) == s_captured_streams.end());
  s_captured_streams[fd] = std::make_unique<CapturedStream>(fd, filename);
}
static inline void CaptureTestStdout() {
  CaptureTestOutput(fileno(stdout), FLAGS_test_tmpdir + "/captured.out");
}
static inline void CaptureTestStderr() {
  CaptureTestOutput(fileno(stderr), FLAGS_test_tmpdir + "/captured.err");
}
// Return the size (in bytes) of a file
static inline size_t GetFileSize(FILE* file) {
  fseek(file, 0, SEEK_END);
  return static_cast<size_t>(ftell(file));
}
// Read the entire content of a file as a string
static inline string ReadEntireFile(FILE* file) {
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
// Get the captured stdout  or stderr as a string
static inline string GetCapturedTestOutput(int fd) {
  CHECK((fd == fileno(stdout)) || (fd == fileno(stderr)));
  std::unique_ptr<CapturedStream> cap = std::move(s_captured_streams.at(fd));
  s_captured_streams.erase(fd);
  CHECK(cap) << ": did you forget CaptureTestStdout() or CaptureTestStderr()?";

  // Make sure everything is flushed.
  cap->StopCapture();

  // Read the captured file.
  std::unique_ptr<FILE> file{fopen(cap->filename().c_str(), "r")};
  const string content = ReadEntireFile(file.get());
  file.reset();

  return content;
}
// Get the captured stderr of a test as a string.
static inline string GetCapturedTestStderr() {
  return GetCapturedTestOutput(fileno(stderr));
}

static const std::size_t kLoggingPrefixLength = 9;

// Check if the string is [IWEF](\d{8}|YEARDATE)
static inline bool IsLoggingPrefix(const string& s) {
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
static inline string MungeLine(const string& line) {
  string before, logcode_date, time, thread_lineinfo;
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
    string tmp;
    iss >> tmp;
    CHECK(!tmp.empty());
    CHECK_EQ(']', tmp[tmp.size() - 1]);
    thread_lineinfo = "THREADID " + tmp;
  }
  size_t index = thread_lineinfo.find(':');
  CHECK_NE(string::npos, index);
  thread_lineinfo = thread_lineinfo.substr(0, index + 1) + "LINE]";
  string rest;
  std::getline(iss, rest);
  return (before + logcode_date[0] + "YEARDATE TIME__ " + thread_lineinfo +
          MungeLine(rest));
}

static inline void StringReplace(string* str, const string& oldsub,
                                 const string& newsub) {
  size_t pos = str->find(oldsub);
  if (pos != string::npos) {
    str->replace(pos, oldsub.size(), newsub);
  }
}

static inline string Munge(const string& filename) {
  std::unique_ptr<FILE> fp{fopen(filename.c_str(), "rb")};
  CHECK(fp != nullptr) << filename << ": couldn't open";
  char buf[4096];
  string result;
  while (fgets(buf, 4095, fp.get())) {
    string line = MungeLine(buf);
    const size_t str_size = 256;
    char null_str[str_size];
    char ptr_str[str_size];
    std::snprintf(null_str, str_size, "%p", static_cast<void*>(nullptr));
    std::snprintf(ptr_str, str_size, "%p",
                  reinterpret_cast<void*>(PTR_TEST_VALUE));

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

static inline void WriteToFile(const string& body, const string& file) {
  std::unique_ptr<FILE> fp{fopen(file.c_str(), "wb")};
  fwrite(body.data(), 1, body.size(), fp.get());
}

static inline bool MungeAndDiffTest(const string& golden_filename,
                                    CapturedStream* cap) {
  auto pos = s_captured_streams.find(fileno(stdout));

  if (pos != s_captured_streams.end() && cap == pos->second.get()) {
    CHECK(cap) << ": did you forget CaptureTestStdout()?";
  } else {
    CHECK(cap) << ": did you forget CaptureTestStderr()?";
  }

  cap->StopCapture();

  // Run munge
  const string captured = Munge(cap->filename());
  const string golden = Munge(golden_filename);
  if (captured != golden) {
    fprintf(stderr,
            "Test with golden file failed. We'll try to show the diff:\n");
    string munged_golden = golden_filename + ".munged";
    WriteToFile(golden, munged_golden);
    string munged_captured = cap->filename() + ".munged";
    WriteToFile(captured, munged_captured);
#ifdef NGLOG_OS_WINDOWS
    string diffcmd("fc " + munged_golden + " " + munged_captured);
#else
    string diffcmd("diff -u " + munged_golden + " " + munged_captured);
#endif
    if (system(diffcmd.c_str()) != 0) {
      fprintf(stderr, "diff command was failed.\n");
    }
    unlink(munged_golden.c_str());
    unlink(munged_captured.c_str());
    return false;
  }
  LOG(INFO) << "Diff was successful";
  return true;
}

static inline bool MungeAndDiffTestStderr(const string& golden_filename) {
  return MungeAndDiffTest(golden_filename,
                          s_captured_streams.at(fileno(stderr)).get());
}

static inline bool MungeAndDiffTestStdout(const string& golden_filename) {
  return MungeAndDiffTest(golden_filename,
                          s_captured_streams.at(fileno(stdout)).get());
}

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
#endif

// Add hook for operator new to ensure there are no memory allocation.

void (*g_new_hook)() = nullptr;

}  // namespace nglog

void* operator new(size_t size, const std::nothrow_t&) noexcept {
  if (nglog::g_new_hook) {
    nglog::g_new_hook();
  }
  return malloc(size);
}

void* operator new(size_t size) NGLOG_GLOG_THROW_BAD_ALLOC {
  void* p = ::operator new(size, std::nothrow);
  if (p == nullptr) {
    throw std::bad_alloc{};
  }
  return p;
}

void* operator new[](size_t size) NGLOG_GLOG_THROW_BAD_ALLOC {
  return ::operator new(size);
}

void operator delete(void* p) noexcept { free(p); }

void operator delete(void* p, size_t) noexcept { ::operator delete(p); }

void operator delete[](void* p) noexcept { ::operator delete(p); }

void operator delete[](void* p, size_t) noexcept { ::operator delete(p); }
