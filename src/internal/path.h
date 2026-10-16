// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause

#ifndef NGLOG_INTERNAL_PATH_H_
#define NGLOG_INTERNAL_PATH_H_

#include "ng-log/platform.h"

#ifndef NGLOG_OS_WINDOWS

#  include <sys/types.h>

#  include <cstddef>
#  include <string>
#  include <vector>

#  include "ng-log/export.h"

struct stat;

namespace nglog {
namespace internal {

using PosixDirectoryEntryCallback = bool (*)(const char* entry,
                                             std::size_t length, void* context);

NGLOG_NO_EXPORT int StatPosixPath(const std::string& path, struct stat* result);
NGLOG_NO_EXPORT int OpenPosixPath(const std::string& path, int flags,
                                  mode_t mode);
NGLOG_NO_EXPORT int UnlinkPosixPath(const std::string& path);
NGLOG_NO_EXPORT int AccessPosixPath(const std::string& path, int mode);
NGLOG_NO_EXPORT bool ForEachPosixDirectory(const std::string& path,
                                           PosixDirectoryEntryCallback callback,
                                           void* context);
NGLOG_NO_EXPORT bool ListPosixDirectory(const std::string& path,
                                        std::vector<std::string>* entries);
NGLOG_NO_EXPORT int SymlinkPosixPath(const char* target,
                                     std::size_t target_length,
                                     const std::string& link_path);
NGLOG_NO_EXPORT int TruncatePosixPath(const std::string& path);

}  // namespace internal
}  // namespace nglog

#endif  // !NGLOG_OS_WINDOWS

#ifdef NGLOG_OS_WINDOWS
#  include <string>

#  include "internal/utf8.h"
#endif

namespace nglog {
namespace internal {

/**
 * @name Platform-independent path operations
 *
 * Windows paths are interpreted as UTF-8. POSIX paths are passed through
 * unchanged and retried relative to directory descriptors when they exceed
 * the system path length limit.
 * @{
 */

inline int StatPath(const std::string& path, struct stat* result) {
#ifdef NGLOG_OS_WINDOWS
  return StatUtf8(path.data(), path.size(), result);
#else
  return StatPosixPath(path, result);
#endif
}

inline int OpenPath(const std::string& path, int flags, int mode) {
#ifdef NGLOG_OS_WINDOWS
  return OpenUtf8(path.data(), path.size(), flags, mode);
#else
  return OpenPosixPath(path, flags, static_cast<mode_t>(mode));
#endif
}

inline int UnlinkPath(const std::string& path) {
#ifdef NGLOG_OS_WINDOWS
  return UnlinkUtf8(path.data(), path.size());
#else
  return UnlinkPosixPath(path);
#endif
}

inline int AccessPath(const std::string& path, int mode) {
#ifdef NGLOG_OS_WINDOWS
  return AccessUtf8(path.data(), path.size(), mode);
#else
  return AccessPosixPath(path, mode);
#endif
}

inline bool ForEachDirectoryPath(const std::string& path,
                                 bool (*callback)(const char* entry,
                                                  std::size_t length,
                                                  void* context),
                                 void* context) {
#ifdef NGLOG_OS_WINDOWS
  return ForEachDirectoryEntryUtf8(path.data(), path.size(), callback, context);
#else
  return ForEachPosixDirectory(path, callback, context);
#endif
}

/** @} */

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_PATH_H_
