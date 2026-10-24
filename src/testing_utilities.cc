// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause

#include "testing_utilities.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <new>
#include <sstream>
#include <vector>

#include "config.h"
#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "ng-log/platform.h"
#include "stacktrace.h"

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

namespace {

// Reads a text file. Line endings are translated as for a text stream, so
// that captured output uses the same line endings on every platform.
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

void (*g_new_hook)() = nullptr;
void (*g_new_size_hook)(std::size_t) = nullptr;
void (*g_allocation_hook)(void* pointer, std::size_t size) = nullptr;
void (*g_deallocation_hook)(void* pointer) = nullptr;

int CountFramesBeforeCutOffMarker(const std::string& output) {
  // DumpStackFrameInfo() and DumpStackTrace() start each frame with it.
  constexpr char kFramePrefix[] = "    @ ";
  const std::string marker = TrimTrailingCRLF(kStackDumpCutOffMarker);
  int frames = 0;
  std::string::size_type begin = 0;
  while (begin < output.size()) {
    std::string::size_type end = output.find('\n', begin);
    if (end == std::string::npos) {
      end = output.size();
    }
    const std::string line =
        TrimTrailingCRLF(output.substr(begin, end - begin));
    if (line == marker) {
      return frames;
    }
    if (line.compare(0, sizeof(kFramePrefix) - 1, kFramePrefix) == 0) {
      ++frames;
    } else {
      frames = 0;
    }
    begin = end + 1;
  }
  return kNoCutOffMarker;
}

#if defined(HAVE_STACKTRACE)
// Recurses `depth` frames, then returns how many frames the unwinder walks,
// up to kMaxStackDumpFrames + 1.
NGLOG_ATTRIBUTE_NOINLINE static int UnwoundDepthAt(int depth) {
  if (depth > 0) {
    const int result = UnwoundDepthAt(depth - 1);
    NGLOG_BLOCK_TAIL_CALL_OPTIMIZATION();
    return result;
  }
  void* stack[kMaxStackDumpFrames + 1];
  return GetStackTrace(stack, ARRAYSIZE(stack), 0);
}

bool UnwinderWalksPastMaxStackDumpFrames() {
  return UnwoundDepthAt(kDepthPastStackDumpLimit) > kMaxStackDumpFrames;
}
#endif  // defined(HAVE_STACKTRACE)

}  // namespace nglog

// noinline prevents a -Wmismatched-new-delete false positive: GCC loses
// track of the malloc/free pairing across inlined operator new/delete.
// See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=103993
[[gnu::noinline]] void* operator new(std::size_t size,
                                     const std::nothrow_t&) noexcept {
  if (nglog::g_new_hook) {
    nglog::g_new_hook();
  }
  if (nglog::g_new_size_hook) {
    nglog::g_new_size_hook(size);
  }
  void* p = malloc(size);
  if (p != nullptr && nglog::g_allocation_hook) {
    nglog::g_allocation_hook(p, size);
  }
  return p;
}

[[gnu::noinline]] void* operator new(std::size_t size) {
  if (nglog::g_new_hook) {
    nglog::g_new_hook();
  }
  if (nglog::g_new_size_hook) {
    nglog::g_new_size_hook(size);
  }
  void* p = malloc(size);
  if (p == nullptr) {
    throw std::bad_alloc{};
  }
  if (nglog::g_allocation_hook) {
    nglog::g_allocation_hook(p, size);
  }
  return p;
}

[[gnu::noinline]] void* operator new[](std::size_t size) {
  if (nglog::g_new_hook) {
    nglog::g_new_hook();
  }
  if (nglog::g_new_size_hook) {
    nglog::g_new_size_hook(size);
  }
  void* p = malloc(size);
  if (p == nullptr) {
    throw std::bad_alloc{};
  }
  if (nglog::g_allocation_hook) {
    nglog::g_allocation_hook(p, size);
  }
  return p;
}

[[gnu::noinline]] void operator delete(void* p) noexcept {
  if (nglog::g_deallocation_hook) {
    nglog::g_deallocation_hook(p);
  }
  free(p);
}

[[gnu::noinline]] void operator delete(void* p, std::size_t) noexcept {
  if (nglog::g_deallocation_hook) {
    nglog::g_deallocation_hook(p);
  }
  free(p);
}

[[gnu::noinline]] void operator delete[](void* p) noexcept {
  if (nglog::g_deallocation_hook) {
    nglog::g_deallocation_hook(p);
  }
  free(p);
}

[[gnu::noinline]] void operator delete[](void* p, std::size_t) noexcept {
  if (nglog::g_deallocation_hook) {
    nglog::g_deallocation_hook(p);
  }
  free(p);
}
