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
#include <fstream>
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

namespace {

// Reads a text file. Line endings are translated as for a text stream, so
// that captured output and golden files compare equally on every platform.
bool ReadTextFile(const std::string& path, std::string* contents) {
  std::ifstream file{path};
  if (!file) {
    return false;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  *contents = buffer.str();
  return true;
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

// Writes a diff of two line sequences that shows each change together with
// the unchanged lines around it.
void PrintDiff(const std::vector<std::string>& golden,
               const std::vector<std::string>& actual, std::ostream& output) {
  const std::size_t rows = golden.size();
  const std::size_t cols = actual.size();

  // lcs[i][j] holds the length of the longest common subsequence of
  // golden[i:] and actual[j:].
  std::vector<std::vector<std::size_t>> lcs(
      rows + 1, std::vector<std::size_t>(cols + 1, 0));
  for (std::size_t i = rows; i-- > 0;) {
    for (std::size_t j = cols; j-- > 0;) {
      lcs[i][j] = golden[i] == actual[j]
                      ? lcs[i + 1][j + 1] + 1
                      : std::max(lcs[i + 1][j], lcs[i][j + 1]);
    }
  }

  // Each line is prefixed with "  " if unchanged, "- " if only in the golden
  // file and "+ " if only in the actual output.
  std::vector<std::string> lines;
  std::size_t i = 0;
  std::size_t j = 0;
  while (i < rows || j < cols) {
    if (i < rows && j < cols && golden[i] == actual[j]) {
      lines.push_back("  " + golden[i]);
      ++i;
      ++j;
    } else if (j == cols || (i < rows && lcs[i + 1][j] >= lcs[i][j + 1])) {
      lines.push_back("- " + golden[i]);
      ++i;
    } else {
      lines.push_back("+ " + actual[j]);
      ++j;
    }
  }

  constexpr std::size_t kContextLines = 3;
  std::vector<bool> shown(lines.size(), false);
  for (std::size_t k = 0; k < lines.size(); ++k) {
    if (lines[k][0] != ' ') {
      const std::size_t first = k > kContextLines ? k - kContextLines : 0;
      const std::size_t last = std::min(lines.size(), k + kContextLines + 1);
      std::fill(shown.begin() + static_cast<std::ptrdiff_t>(first),
                shown.begin() + static_cast<std::ptrdiff_t>(last), true);
    }
  }
  bool skipping = false;
  for (std::size_t k = 0; k < lines.size(); ++k) {
    if (shown[k]) {
      output << lines[k] << '\n';
      skipping = false;
    } else if (!skipping) {
      output << "...\n";
      skipping = true;
    }
  }
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

std::string Munge(const std::string& text) {
  constexpr std::size_t kPointerBufferSize = 256;
  char null_str[kPointerBufferSize];
  char ptr_str[kPointerBufferSize];
  std::snprintf(null_str, sizeof(null_str), "%p", static_cast<void*>(nullptr));
  std::snprintf(ptr_str, sizeof(ptr_str), "%p",
                reinterpret_cast<void*>(kPtrTestValue));

  std::istringstream input{text};
  std::string buf;
  std::string result;
  while (std::getline(input, buf)) {
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

}  // namespace

ScopedStreamCapture::ScopedStreamCapture(StandardStream stream)
    : stream_{stream == StandardStream::kStdout ? stdout : stderr} {
  static unsigned int captures = 0;
  filename_ = TestTmpDir() + "/captured-" + std::to_string(++captures) +
              (stream == StandardStream::kStdout ? ".out" : ".err");
  const int fd = fileno(stream_);
  std::fflush(nullptr);
  original_.reset(dup(fd));
  CHECK(original_) << ": cannot duplicate descriptor " << fd;
  const FileDescriptor capture{
      open(filename_.c_str(), O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR)};
  CHECK(capture) << ": cannot create " << filename_;
  CHECK_NE(dup2(capture.get(), fd), -1)
      << ": cannot redirect descriptor " << fd << " to " << filename_;
}

ScopedStreamCapture::~ScopedStreamCapture() { Restore(); }

void ScopedStreamCapture::Restore() {
  if (original_) {
    std::fflush(nullptr);
    CHECK_NE(dup2(original_.get(), fileno(stream_)), -1)
        << ": cannot restore descriptor " << fileno(stream_);
    original_.reset();
  }
}

std::string ScopedStreamCapture::Stop() {
  Restore();
  return Peek();
}

std::string ScopedStreamCapture::Peek() const {
  std::string output;
  CHECK(ReadTextFile(filename_, &output)) << ": cannot read " << filename_;
  return output;
}

testing::AssertionResult MatchesGoldenFile(const std::string& output,
                                           const std::string& golden_filename) {
  std::string golden;
  if (!ReadTextFile(golden_filename, &golden)) {
    return testing::AssertionFailure()
           << "cannot read the golden file " << golden_filename;
  }
  const std::string normalized_golden = Munge(golden);
  const std::string normalized_output = Munge(output);
  if (normalized_output == normalized_golden) {
    return testing::AssertionSuccess();
  }
  std::ostringstream diff;
  PrintDiff(SplitLines(normalized_golden), SplitLines(normalized_output), diff);
  return testing::AssertionFailure()
         << "the output differs from the golden file " << golden_filename
         << " (- golden, + actual):\n"
         << diff.str();
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
