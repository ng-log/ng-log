// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause

#include "testing_utilities.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
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

#ifdef NGLOG_OS_WINDOWS
#  include "internal/utf8.h"
#else
#  include <ftw.h>
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

#ifdef NGLOG_OS_WINDOWS
//! Removes a directory together with its contents.
void RemoveDirectoryTree(const std::wstring& path) {
  WIN32_FIND_DATAW entry;
  const HANDLE find = FindFirstFileW((path + L"\\*").c_str(), &entry);
  if (find != INVALID_HANDLE_VALUE) {
    do {
      const std::wstring name = entry.cFileName;
      if (name == L"." || name == L"..") {
        continue;
      }
      const std::wstring child = path + L'\\' + name;
      const DWORD attributes = entry.dwFileAttributes;
      if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        DeleteFileW(child.c_str());
      } else if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        // Removes the link without following it.
        RemoveDirectoryW(child.c_str());
      } else {
        RemoveDirectoryTree(child);
      }
    } while (FindNextFileW(find, &entry));
    FindClose(find);
  }
  RemoveDirectoryW(path.c_str());
}

//! Directory that is created on construction and removed with its contents.
class PrivateDirectory final {
 public:
  explicit PrivateDirectory(const std::string& parent) {
    const std::string prefix =
        parent + "/nglog-test-" + std::to_string(GetCurrentProcessId()) + '-';
    // Directories left behind by an earlier process with the same identifier
    // are skipped.
    for (unsigned attempt = 0;; ++attempt) {
      path_ = prefix + std::to_string(attempt);
      if (!nglog::internal::Utf8ToWide(path_.data(), path_.size(),
                                       &wide_path_)) {
        std::fprintf(stderr, "Could not convert %s to UTF-16\n", path_.c_str());
        std::exit(EXIT_FAILURE);
      }
      if (CreateDirectoryW(wide_path_.c_str(), nullptr)) {
        return;
      }
      const DWORD error = GetLastError();
      if (error != ERROR_ALREADY_EXISTS) {
        std::fprintf(stderr, "Could not create directory %s: error %lu\n",
                     path_.c_str(), static_cast<unsigned long>(error));
        std::exit(EXIT_FAILURE);
      }
    }
  }

  ~PrivateDirectory() { RemoveDirectoryTree(wide_path_); }

  PrivateDirectory(const PrivateDirectory& other) = delete;
  PrivateDirectory& operator=(const PrivateDirectory& other) = delete;

  const std::string& path() const noexcept { return path_; }

 private:
  std::string path_;
  std::wstring wide_path_;
};
#else
int RemoveDirectoryEntry(const char* path, const struct stat* /*status*/,
                         int /*type*/, struct FTW* /*position*/) {
  return std::remove(path);
}

//! Directory that is created on construction and removed with its contents.
class PrivateDirectory final {
 public:
  explicit PrivateDirectory(const std::string& parent)
      : path_{parent + "/nglog-test-XXXXXX"}, owner_{getpid()} {
    if (mkdtemp(&path_[0]) == nullptr) {
      std::perror(("Could not create directory in " + parent).c_str());
      std::exit(EXIT_FAILURE);
    }
  }

  ~PrivateDirectory() {
    // Forked children run the same static destructors on exit and must not
    // remove the directory of their parent.
    if (getpid() == owner_) {
      constexpr int kMaximumOpenDescriptors = 16;
      nftw(path_.c_str(), RemoveDirectoryEntry, kMaximumOpenDescriptors,
           FTW_DEPTH | FTW_PHYS);
    }
  }

  PrivateDirectory(const PrivateDirectory& other) = delete;
  PrivateDirectory& operator=(const PrivateDirectory& other) = delete;

  const std::string& path() const noexcept { return path_; }

 private:
  std::string path_;
  pid_t owner_;
};
#endif  // NGLOG_OS_WINDOWS

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

const std::string& SystemTmpDir() {
  static const std::string dir = GetTempDir();
  return dir;
}

const std::string& TestTmpDir() {
  // Each test process uses a directory that no other process modifies. Test
  // files with fixed names therefore cannot collide between concurrent tests,
  // and directory listings on Emscripten, which end early when an entry
  // vanishes while the listing is in progress, stay complete.
  static const PrivateDirectory dir{SystemTmpDir()};
  return dir.path();
}

void InitializeTestLogging(const char* program) {
  SetFlag(FLAGS_log_dir, TestTmpDir());
  InitializeLogging(program);
}

bool SetEnvironmentValue(const char* name, const char* value) {
#ifdef NGLOG_OS_WINDOWS
  // An empty value removes the variable on Windows.
  return _putenv_s(name, value != nullptr ? value : "") == 0;
#else
  if (value != nullptr) {
    return setenv(name, value, 1) == 0;
  }
  return unsetenv(name) == 0;
#endif  // NGLOG_OS_WINDOWS
}

ScopedEnvironmentVariable::ScopedEnvironmentVariable(const char* name,
                                                     const char* value)
    : name_{name} {
  const char* const previous = std::getenv(name);
  had_previous_ = previous != nullptr;
  if (had_previous_) {
    previous_ = previous;
  }
  CHECK(SetEnvironmentValue(name, value)) << ": cannot set " << name_;
}

ScopedEnvironmentVariable::~ScopedEnvironmentVariable() {
  CHECK(SetEnvironmentValue(name_.c_str(),
                            had_previous_ ? previous_.c_str() : nullptr))
      << ": cannot restore " << name_;
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
      lines.emplace_back(s, start);
      break;
    }
    lines.emplace_back(s, start, pos - start);
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

constexpr char kDatePlaceholder[] = "YEARDATE";
constexpr std::size_t kDateLength = sizeof(kDatePlaceholder) - 1;
//! Severity character followed by the date.
constexpr std::size_t kLoggingPrefixLength = 1 + kDateLength;

// Check if the string is [IWEF](\d{8}|YEARDATE)
bool IsLoggingPrefix(const std::string& s, std::size_t offset) {
  if (offset > s.size() || s.size() - offset < kLoggingPrefixLength) {
    return false;
  }
  if (s[offset] == '\0' || !strchr("IWEF", s[offset])) {
    return false;
  }
  for (std::size_t i = 1; i <= kDateLength; ++i) {
    // Passing a negative char such as one from non-ASCII log content to
    // isdigit() is undefined behavior, hence the cast to unsigned char.
    if (!isdigit(static_cast<unsigned char>(s[offset + i])) &&
        s[offset + i] != kDatePlaceholder[i - 1]) {
      return false;
    }
  }
  return true;
}

// Convert log output into normalized form.
//
// Example:
//     I20200102 030405 logging_unittest.cc:345] RAW: vlog -1
//  => IYEARDATE TIME__ logging_unittest.cc:LINE] RAW: vlog -1
std::string MungeLine(const std::string& line) {
  std::string before;
  std::string logcode_date;
  std::string time;
  std::string thread_lineinfo;
  std::size_t begin_of_logging_prefix = 0;
  for (; begin_of_logging_prefix + kLoggingPrefixLength < line.size();
       ++begin_of_logging_prefix) {
    if (IsLoggingPrefix(line, begin_of_logging_prefix)) {
      break;
    }
  }
  if (begin_of_logging_prefix + kLoggingPrefixLength >= line.size()) {
    return line;
  } else if (begin_of_logging_prefix > 0) {
    before.assign(line, 0, begin_of_logging_prefix - 1);
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
  thread_lineinfo.resize(index + 1);
  thread_lineinfo += "LINE]";
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
  constexpr std::size_t kPointerBufferSize = 256;
  char null_str[kPointerBufferSize];
  char ptr_str[kPointerBufferSize];
  std::snprintf(null_str, sizeof(null_str), "%p", static_cast<void*>(nullptr));
  std::snprintf(ptr_str, sizeof(ptr_str), "%p",
                reinterpret_cast<void*>(kPtrTestValue));

  constexpr std::size_t kLineBufferSize = 4096;
  char buf[kLineBufferSize];
  std::string result;
  while (fgets(buf, sizeof(buf) - 1, fp.get())) {
    std::string line = MungeLine(buf);
    StringReplace(&line, "__NULLP__", null_str);
    StringReplace(&line, "__PTRTEST__", ptr_str);

    // Error numbers differ between platforms. Describe the number PLOG
    // printed in brackets.
    constexpr char kStrErrorPlaceholder[] = "__STRERROR__";
    const std::string::size_type strerror_pos = line.find(kStrErrorPlaceholder);
    if (strerror_pos != std::string::npos) {
      const std::string::size_type errnum_pos = line.find('[', strerror_pos);
      CHECK(errnum_pos != std::string::npos)
          << "missing error number after " << kStrErrorPlaceholder << " in "
          << line;
      const int errnum = std::atoi(line.c_str() + errnum_pos + 1);
      StringReplace(&line, kStrErrorPlaceholder, StrError(errnum));
    }
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
