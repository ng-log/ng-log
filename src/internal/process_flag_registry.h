// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_PROCESS_FLAG_REGISTRY_H
#define NGLOG_INTERNAL_PROCESS_FLAG_REGISTRY_H

#include <string>

namespace nglog {
namespace internal {

struct ProcessFlagInfo {
  std::string name;
  std::string filename;
  std::string type;
};

enum class ProcessFlagRegistration {
  kRegistered,
  kDuplicate,
  kUnavailable,
};

/**
 * Coordinates flag names between independent ng-log library instances in one
 * process.
 *
 * The first instance allocates the shared state in memory and publishes its
 * address through an operating-system object that only the process can access:
 * a mapping of an anonymous memory file on Linux and a named file mapping on
 * Windows. Other instances in the same process find the state through that
 * object. Neither the environment nor open file descriptors are involved, so
 * clearing the environment or closing descriptors does not affect the
 * detection. On other platforms, or if the object refers to memory that is not
 * a registry, each library instance keeps its own state.
 */
class ProcessFlagRegistry {
 public:
  ProcessFlagRegistry() = default;
  ProcessFlagRegistry(const ProcessFlagRegistry&) = delete;
  ProcessFlagRegistry& operator=(const ProcessFlagRegistry&) = delete;
  ~ProcessFlagRegistry();

  ProcessFlagRegistration Register(const char* name, const char* filename,
                                   const char* type, ProcessFlagInfo* previous);
  void Unregister(const char* name, const char* filename);
};

// Hold and release the lock of the shared state around fork, so that a child
// process never inherits it in a locked state.
void LockProcessFlagRegistryForFork();
void UnlockProcessFlagRegistryAfterFork();

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_PROCESS_FLAG_REGISTRY_H
