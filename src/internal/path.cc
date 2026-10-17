// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE 1
#endif

#include "internal/path.h"

#include "config.h"
#include "ng-log/platform.h"
#include "utilities.h"

#ifndef NGLOG_OS_WINDOWS

#  include <dirent.h>
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <unistd.h>

#  include <cerrno>
#  include <cstddef>
#  include <cstring>
#  include <string>
#  include <utility>
#  include <vector>

namespace nglog {
namespace internal {
namespace {

class Directory final {
 public:
  Directory() = default;
  explicit Directory(DIR* directory) : directory_(directory) {}
  Directory(const Directory&) = delete;
  Directory& operator=(const Directory&) = delete;

  Directory(Directory&& other) noexcept : directory_(other.release()) {}

  Directory& operator=(Directory&& other) noexcept {
    reset(other.release());
    return *this;
  }

  ~Directory() { reset(); }

  explicit operator bool() const { return directory_ != nullptr; }
  DIR* get() const { return directory_; }

  DIR* release() { return std::exchange(directory_, nullptr); }

  int close() {
    DIR* const directory = release();
    return directory == nullptr ? 0 : closedir(directory);
  }

  void reset(DIR* directory = nullptr) {
    const int saved_errno = errno;
    static_cast<void>(close());
    directory_ = directory;
    errno = saved_errno;
  }

 private:
  DIR* directory_ = nullptr;
};

bool IsValidPath(const std::string& path) {
  if (path.find('\0') == std::string::npos) {
    return true;
  }
  errno = EINVAL;
  return false;
}

#  ifdef HAVE_POSIX_LONG_PATH_SUPPORT

// Path resolution through directory descriptors performs the same
// permission checks as the direct operation, so only a pathname that exceeds
// the system limit can succeed on retry.
bool ShouldRetryPathOperation(int error) { return error == ENAMETOOLONG; }

int DirectoryOpenFlags() {
  int flags = O_DIRECTORY;
#    ifdef O_SEARCH
  flags |= O_SEARCH;
#    elif defined(O_PATH)
  flags |= O_PATH;
#    elif defined(O_EXEC)
  flags |= O_EXEC;
#    else
  flags |= O_RDONLY;
#    endif
#    ifdef O_CLOEXEC
  flags |= O_CLOEXEC;
#    endif
  return flags;
}

struct ResolvedParent {
  FileDescriptor descriptor;
  std::string final_component;
};

bool ResolveParent(const std::string& path, ResolvedParent* result) {
  const std::size_t component_end = path.find_last_not_of('/');
  if (component_end == std::string::npos) {
    result->descriptor.reset(open("/", DirectoryOpenFlags()));
    result->final_component = ".";
    return static_cast<bool>(result->descriptor);
  }

  const std::size_t component_start = path.rfind('/', component_end);
  const std::size_t final_start =
      component_start == std::string::npos ? 0 : component_start + 1;
  result->final_component = path.substr(final_start);

  const bool absolute = !path.empty() && path.front() == '/';
  FileDescriptor directory{open(absolute ? "/" : ".", DirectoryOpenFlags())};
  if (!directory) {
    return false;
  }

  std::size_t position = absolute ? 1 : 0;
  std::string component;
  while (position < final_start) {
    while (position < final_start && path[position] == '/') {
      ++position;
    }
    if (position >= final_start) {
      break;
    }

    std::size_t separator = path.find('/', position);
    if (separator == std::string::npos || separator > final_start) {
      separator = final_start;
    }
    component.assign(path.data() + position, separator - position);
    if (component == ".") {
      position = separator + 1;
      continue;
    }
    FileDescriptor child{
        openat(directory.get(), component.c_str(), DirectoryOpenFlags())};
    if (!child) {
      return false;
    }
    directory = std::move(child);
    position = separator + 1;
  }

  result->descriptor = std::move(directory);
  return true;
}

template <typename Operation>
int RetryWithResolvedParent(const std::string& path, Operation operation) {
  ResolvedParent resolved;
  if (!ResolveParent(path, &resolved)) {
    return -1;
  }
  return operation(resolved.descriptor.get(), resolved.final_component.c_str());
}

int TruncateOpenFlags() {
  int flags = O_WRONLY | O_TRUNC;
#    ifdef O_LARGEFILE
  flags |= O_LARGEFILE;
#    endif
#    ifdef O_CLOEXEC
  flags |= O_CLOEXEC;
#    endif
  return flags;
}

int DirectoryReadFlags() {
  int flags = O_RDONLY | O_DIRECTORY;
#    ifdef O_CLOEXEC
  flags |= O_CLOEXEC;
#    endif
  return flags;
}

#  endif

Directory OpenPosixDirectory(const std::string& path) {
  Directory directory{opendir(path.c_str())};
#  ifdef HAVE_POSIX_LONG_PATH_SUPPORT
  if (directory || !ShouldRetryPathOperation(errno)) {
    return directory;
  }

  const int descriptor =
      RetryWithResolvedParent(path, [](int parent, const char* component) {
        return openat(parent, component, DirectoryReadFlags());
      });
  if (descriptor == -1) {
    return Directory();
  }

  DIR* const fallback_directory = fdopendir(descriptor);
  if (fallback_directory == nullptr) {
    const int saved_errno = errno;
    close(descriptor);
    errno = saved_errno;
  }
  return Directory{fallback_directory};
#  endif
  return directory;
}

}  // namespace

int StatPosixPath(const std::string& path, struct stat* result) {
  if (!IsValidPath(path)) {
    return -1;
  }
  const int status = stat(path.c_str(), result);
#  ifdef HAVE_POSIX_LONG_PATH_SUPPORT
  if (status == -1 && ShouldRetryPathOperation(errno)) {
    return RetryWithResolvedParent(
        path, [result](int descriptor, const char* component) {
          return fstatat(descriptor, component, result, 0);
        });
  }
#  endif
  return status;
}

int OpenPosixPath(const std::string& path, int flags, mode_t mode) {
  if (!IsValidPath(path)) {
    return -1;
  }
  const int descriptor = open(path.c_str(), flags, mode);
#  ifdef HAVE_POSIX_LONG_PATH_SUPPORT
  if (descriptor == -1 && ShouldRetryPathOperation(errno)) {
    return RetryWithResolvedParent(
        path, [flags, mode](int parent, const char* component) {
          return openat(parent, component, flags, mode);
        });
  }
#  endif
  return descriptor;
}

int UnlinkPosixPath(const std::string& path) {
  if (!IsValidPath(path)) {
    return -1;
  }
  const int status = unlink(path.c_str());
#  ifdef HAVE_POSIX_LONG_PATH_SUPPORT
  if (status == -1 && ShouldRetryPathOperation(errno)) {
    return RetryWithResolvedParent(path,
                                   [](int descriptor, const char* component) {
                                     return unlinkat(descriptor, component, 0);
                                   });
  }
#  endif
  return status;
}

int AccessPosixPath(const std::string& path, int mode) {
  if (!IsValidPath(path)) {
    return -1;
  }
  const int status = access(path.c_str(), mode);
#  ifdef HAVE_POSIX_LONG_PATH_SUPPORT
  if (status == -1 && ShouldRetryPathOperation(errno)) {
    return RetryWithResolvedParent(
        path, [mode](int descriptor, const char* component) {
          return faccessat(descriptor, component, mode, 0);
        });
  }
#  endif
  return status;
}

bool ForEachPosixDirectory(const std::string& path,
                           PosixDirectoryEntryCallback callback,
                           void* context) {
  if (!IsValidPath(path)) {
    return false;
  }
  if (callback == nullptr) {
    errno = EINVAL;
    return false;
  }

  Directory directory = OpenPosixDirectory(path);
  if (!directory) {
    return false;
  }

  // readdir() reports errors only through errno, which callbacks may change,
  // so it is cleared before every call.
  for (;;) {
    errno = 0;
    const struct dirent* const entry = readdir(directory.get());
    if (entry == nullptr) {
      break;
    }
    if (std::strcmp(entry->d_name, ".") == 0 ||
        std::strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    if (!callback(entry->d_name, std::strlen(entry->d_name), context)) {
      const int callback_errno = errno;
      static_cast<void>(directory.close());
      errno = callback_errno;
      return false;
    }
  }
  const int read_errno = errno;
  const int close_status = directory.close();
  if (read_errno != 0) {
    errno = read_errno;
    return false;
  }
  if (close_status != 0) {
    return false;
  }
  return true;
}

namespace {

bool AppendDirectoryEntry(const char* entry, std::size_t length,
                          void* context) {
  auto* const entries = static_cast<std::vector<std::string>*>(context);
  entries->emplace_back(entry, length);
  return true;
}

}  // namespace

bool ListPosixDirectory(const std::string& path,
                        std::vector<std::string>* entries) {
  if (entries == nullptr) {
    errno = EINVAL;
    return false;
  }
  entries->clear();
  return ForEachPosixDirectory(path, AppendDirectoryEntry, entries);
}

int SymlinkPosixPath(const char* target, std::size_t target_length,
                     const std::string& link_path) {
  if (target == nullptr && target_length != 0) {
    errno = EINVAL;
    return -1;
  }
  const std::string target_string(target == nullptr ? "" : target,
                                  target_length);
  if (!IsValidPath(target_string) || !IsValidPath(link_path)) {
    return -1;
  }
  const int status = symlink(target_string.c_str(), link_path.c_str());
#  ifdef HAVE_POSIX_LONG_PATH_SUPPORT
  if (status == -1 && ShouldRetryPathOperation(errno)) {
    return RetryWithResolvedParent(
        link_path, [&target_string](int descriptor, const char* component) {
          return symlinkat(target_string.c_str(), descriptor, component);
        });
  }
#  endif
  return status;
}

int TruncatePosixPath(const std::string& path) {
  if (!IsValidPath(path)) {
    return -1;
  }
  const int status = truncate(path.c_str(), 0);
#  ifdef HAVE_POSIX_LONG_PATH_SUPPORT
  if (status == -1 && ShouldRetryPathOperation(errno)) {
    const int descriptor =
        RetryWithResolvedParent(path, [](int parent, const char* component) {
          return openat(parent, component, TruncateOpenFlags());
        });
    if (descriptor == -1) {
      return -1;
    }
    const int close_status = close(descriptor);
    return close_status == -1 ? -1 : 0;
  }
#  endif
  return status;
}

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_OS_WINDOWS
