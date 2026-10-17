// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "process_flag_registry.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "ng-log/platform.h"

#ifdef NGLOG_OS_WINDOWS
#  include <windows.h>
#else
#  include <pthread.h>
#endif

#ifdef NGLOG_OS_LINUX
#  include <sys/mman.h>
#  include <sys/syscall.h>
#  include <unistd.h>
#endif

#if defined(__SANITIZE_ADDRESS__)
#  define NGLOG_LEAK_SANITIZER 1
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer) || __has_feature(leak_sanitizer)
#    define NGLOG_LEAK_SANITIZER 1
#  endif
#endif

#ifdef NGLOG_LEAK_SANITIZER
#  include <sanitizer/lsan_interface.h>
#endif

namespace nglog {
namespace internal {
namespace {

// The layout of the shared state is part of the contract between library
// instances of possibly different versions. Any change to SharedRegistry,
// SharedEntry, or Anchor requires a new anchor name and magic value.
constexpr std::uint64_t kSharedRegistryMagic = 0x6e676c6f67666c32;  // nglogfl2

struct SharedEntry {
  SharedEntry* next;
  const void* owner;
  const char* name;
  const char* filename;
  const char* type;
};

struct SharedRegistry {
  std::uint64_t magic;
#ifdef NGLOG_OS_WINDOWS
  SRWLOCK lock;
#else
  pthread_mutex_t lock;
#endif
  SharedEntry* head;
};

// The memory that library instances use to find the shared registry. It is
// private to the process and holds the registry address.
struct Anchor {
  std::atomic<std::uintptr_t> registry;
};

// Memory is taken from an allocator that every library instance shares, so
// that an entry allocated by one instance can be released by another one.
void* Allocate(std::size_t size) {
#ifdef NGLOG_OS_WINDOWS
  return HeapAlloc(GetProcessHeap(), 0, size);
#else
  return std::malloc(size);
#endif
}

void Deallocate(void* memory) {
#ifdef NGLOG_OS_WINDOWS
  HeapFree(GetProcessHeap(), 0, memory);
#else
  std::free(memory);
#endif
}

void LockSharedRegistry(SharedRegistry& registry) {
#ifdef NGLOG_OS_WINDOWS
  AcquireSRWLockExclusive(&registry.lock);
#else
  pthread_mutex_lock(&registry.lock);
#endif
}

void UnlockSharedRegistry(SharedRegistry& registry) {
#ifdef NGLOG_OS_WINDOWS
  ReleaseSRWLockExclusive(&registry.lock);
#else
  pthread_mutex_unlock(&registry.lock);
#endif
}

class SharedRegistryLock {
 public:
  explicit SharedRegistryLock(SharedRegistry& registry) : registry_(registry) {
    LockSharedRegistry(registry_);
  }

  SharedRegistryLock(const SharedRegistryLock&) = delete;
  SharedRegistryLock& operator=(const SharedRegistryLock&) = delete;

  ~SharedRegistryLock() { UnlockSharedRegistry(registry_); }

 private:
  SharedRegistry& registry_;
};

SharedRegistry* CreateSharedRegistry() {
  auto* const registry =
      static_cast<SharedRegistry*>(Allocate(sizeof(SharedRegistry)));
  if (registry == nullptr) {
    return nullptr;
  }
  registry->magic = kSharedRegistryMagic;
#ifdef NGLOG_OS_WINDOWS
  InitializeSRWLock(&registry->lock);
#else
  if (pthread_mutex_init(&registry->lock, nullptr) != 0) {
    Deallocate(registry);
    return nullptr;
  }
#endif
  registry->head = nullptr;
  return registry;
}

#if (defined(NGLOG_OS_LINUX) && defined(SYS_memfd_create)) || \
    defined(NGLOG_OS_WINDOWS)
void DestroySharedRegistry(SharedRegistry* registry) {
#  ifndef NGLOG_OS_WINDOWS
  pthread_mutex_destroy(&registry->lock);
#  endif
  Deallocate(registry);
}

// Reports whether an address taken from the anchor can be read as a registry.
// On Windows, another process in the same session could create the anchor
// first and choose the address, so the memory is checked before it is read.
bool IsRegistryAddress(const SharedRegistry* registry) {
  const auto address = reinterpret_cast<std::uintptr_t>(registry);
  if (address == 0 || address % alignof(SharedRegistry) != 0) {
    return false;
  }
#  ifdef NGLOG_OS_WINDOWS
  constexpr DWORD kWritable = PAGE_READWRITE | PAGE_EXECUTE_READWRITE;
  constexpr DWORD kInaccessible = PAGE_GUARD | PAGE_NOACCESS;
  MEMORY_BASIC_INFORMATION memory;
  if (VirtualQuery(registry, &memory, sizeof(memory)) != sizeof(memory) ||
      memory.State != MEM_COMMIT || memory.Type != MEM_PRIVATE ||
      (memory.Protect & kWritable) == 0 ||
      (memory.Protect & kInaccessible) != 0) {
    return false;
  }
  const std::uintptr_t end =
      reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
  return address + sizeof(SharedRegistry) <= end;
#  else
  return true;
#  endif
}

// Publishes the candidate unless another library instance published a
// registry first. Returns the published registry. The candidate remains a
// private registry if the anchor refers to memory that is not a registry.
SharedRegistry* PublishSharedRegistry(Anchor& anchor,
                                      SharedRegistry* candidate) {
  std::uintptr_t expected = 0;
  if (anchor.registry.compare_exchange_strong(
          expected, reinterpret_cast<std::uintptr_t>(candidate),
          std::memory_order_acq_rel)) {
    return candidate;
  }
  auto* const published = reinterpret_cast<SharedRegistry*>(expected);
  if (!IsRegistryAddress(published) ||
      published->magic != kSharedRegistryMagic) {
    return candidate;
  }
  DestroySharedRegistry(candidate);
  return published;
}
#endif

#if defined(NGLOG_OS_LINUX) && defined(SYS_memfd_create)

// The anchor is a private mapping of an anonymous memory file. Every library
// instance in the process finds it among the mappings of the process. Being
// private, the mapping is never shared with another process, not even with a
// child created by fork, which receives its own copy. The descriptor is closed
// right after the file is mapped, so that closing descriptors does not affect
// the detection. The mapping is removed when the process replaces its image.
constexpr char kAnchorName[] = "nglog-process-flag-registry-v2";
constexpr char kAnchorPathPrefix[] = "/memfd:";
constexpr char kMappingsPath[] = "/proc/self/maps";
constexpr unsigned kMemfdCloseOnExec = 1U;  // MFD_CLOEXEC
constexpr char kAnchorPermissions[] = "rw-p";
constexpr char kDeletedSuffix[] = " (deleted)";

// Reports whether a line of /proc/self/maps describes an anchor and stores its
// start address. A line has the form "start-end perms offset device inode
// path". The kernel appends " (deleted)" to the path of an anonymous memory
// file. Only a private, writable mapping with exactly that path is an anchor.
bool ParseAnchorMapping(const std::string& line, std::uintptr_t* start) {
  unsigned long long first = 0;
  unsigned long long last = 0;
  char permissions[sizeof(kAnchorPermissions)] = {};
  int path_offset = -1;
  if (std::sscanf(line.c_str(), "%llx-%llx %4s %*s %*s %*s %n", &first, &last,
                  permissions, &path_offset) != 3 ||
      path_offset < 0 || std::strcmp(permissions, kAnchorPermissions) != 0 ||
      last - first < sizeof(Anchor)) {
    return false;
  }
  const std::string path = line.substr(static_cast<std::size_t>(path_offset));
  const std::string expected = std::string(kAnchorPathPrefix) + kAnchorName;
  if (path != expected && path != expected + kDeletedSuffix) {
    return false;
  }
  *start = static_cast<std::uintptr_t>(first);
  return true;
}

// Returns the lowest address at which an anchor is mapped, or null if there is
// none. Instances that create anchors concurrently agree on the lowest one
// only if each maps its anchor before another one searches again. Instances
// acquire the registry while registering their flags during static
// initialization, which the dynamic loader serializes.
Anchor* FindAnchor() {
  std::FILE* const mappings = std::fopen(kMappingsPath, "r");
  if (mappings == nullptr) {
    return nullptr;
  }
  std::uintptr_t lowest = 0;
  std::string line;
  for (int character = std::fgetc(mappings); character != EOF;
       character = std::fgetc(mappings)) {
    if (character != '\n') {
      line.push_back(static_cast<char>(character));
      continue;
    }
    std::uintptr_t start = 0;
    if (ParseAnchorMapping(line, &start) && (lowest == 0 || start < lowest)) {
      lowest = start;
    }
    line.clear();
  }
  std::fclose(mappings);
  return reinterpret_cast<Anchor*>(lowest);
}

Anchor* CreateAnchor() {
  const long fd = syscall(SYS_memfd_create, kAnchorName, kMemfdCloseOnExec);
  if (fd < 0) {
    return nullptr;
  }
  const int descriptor = static_cast<int>(fd);
  void* memory = MAP_FAILED;
  if (ftruncate(descriptor, sizeof(Anchor)) == 0) {
    memory = mmap(nullptr, sizeof(Anchor), PROT_READ | PROT_WRITE, MAP_PRIVATE,
                  descriptor, 0);
  }
  close(descriptor);
  return memory == MAP_FAILED ? nullptr : static_cast<Anchor*>(memory);
}

SharedRegistry* AcquireSharedRegistry() {
  SharedRegistry* const candidate = CreateSharedRegistry();
  if (candidate == nullptr) {
    return nullptr;
  }
  Anchor* anchor = FindAnchor();
  if (anchor == nullptr) {
    Anchor* const created = CreateAnchor();
    if (created == nullptr) {
      return candidate;
    }
    // Another instance may have created an anchor concurrently. The lowest
    // address found wins, and each creator removes its anchor if it lost.
    anchor = FindAnchor();
    if (anchor != created) {
      munmap(created, sizeof(Anchor));
    }
    if (anchor == nullptr) {
      return candidate;
    }
  }
  // The mapping stays for the lifetime of the process, so that instances
  // loaded later find the anchor.
#  ifdef NGLOG_LEAK_SANITIZER
  // Once every library instance that refers to the registry is unloaded, the
  // anchor holds the only reference, but LeakSanitizer does not scan memory
  // files. Its address is published here like any other live reference.
  __lsan_register_root_region(anchor, sizeof(Anchor));
#  endif
  return PublishSharedRegistry(*anchor, candidate);
}

#elif defined(NGLOG_OS_WINDOWS)

// The anchor is a named file mapping in the session namespace. Its name
// contains the process identifier and creation time, which distinguishes it
// from the anchor of an earlier process with the same identifier. The address
// it holds is validated before use, because the name is predictable.
SharedRegistry* AcquireSharedRegistry() {
  SharedRegistry* const candidate = CreateSharedRegistry();
  if (candidate == nullptr) {
    return nullptr;
  }
  FILETIME creation_time;
  FILETIME exit_time;
  FILETIME kernel_time;
  FILETIME user_time;
  if (!GetProcessTimes(GetCurrentProcess(), &creation_time, &exit_time,
                       &kernel_time, &user_time)) {
    return candidate;
  }
  const std::string name = "Local\\nglog-process-flag-registry-v2-" +
                           std::to_string(GetCurrentProcessId()) + "-" +
                           std::to_string(creation_time.dwHighDateTime) + "-" +
                           std::to_string(creation_time.dwLowDateTime);
  // The handle stays open for the lifetime of the process, so that instances
  // loaded later find the anchor.
  const HANDLE mapping =
      CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                         sizeof(Anchor), name.c_str());
  if (mapping == nullptr) {
    return candidate;
  }
  void* const memory =
      MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Anchor));
  if (memory == nullptr) {
    CloseHandle(mapping);
    return candidate;
  }
  return PublishSharedRegistry(*static_cast<Anchor*>(memory), candidate);
}

#else

// Without a process-private anchor, each library instance keeps its own
// registry.
SharedRegistry* AcquireSharedRegistry() { return CreateSharedRegistry(); }

#endif

SharedRegistry* GetSharedRegistry() {
  static SharedRegistry* const registry = AcquireSharedRegistry();
  return registry;
}

SharedEntry* CreateEntry(const void* owner, const char* name,
                         const char* filename, const char* type) {
  const std::size_t name_size = std::strlen(name) + 1;
  const std::size_t filename_size = std::strlen(filename) + 1;
  const std::size_t type_size = std::strlen(type) + 1;
  void* const memory =
      Allocate(sizeof(SharedEntry) + name_size + filename_size + type_size);
  if (memory == nullptr) {
    return nullptr;
  }
  auto* const entry = static_cast<SharedEntry*>(memory);
  char* const strings = static_cast<char*>(memory) + sizeof(SharedEntry);
  std::memcpy(strings, name, name_size);
  std::memcpy(strings + name_size, filename, filename_size);
  std::memcpy(strings + name_size + filename_size, type, type_size);
  entry->next = nullptr;
  entry->owner = owner;
  entry->name = strings;
  entry->filename = strings + name_size;
  entry->type = strings + name_size + filename_size;
  return entry;
}

// Removes the entries owned by the given registry. A null name or filename
// matches every entry.
void RemoveEntries(const void* owner, const char* name, const char* filename) {
  SharedRegistry* const registry = GetSharedRegistry();
  if (registry == nullptr) {
    return;
  }
  const SharedRegistryLock lock(*registry);
  SharedEntry** link = &registry->head;
  while (*link != nullptr) {
    SharedEntry* const entry = *link;
    if (entry->owner == owner &&
        (name == nullptr || std::strcmp(entry->name, name) == 0) &&
        (filename == nullptr || std::strcmp(entry->filename, filename) == 0)) {
      *link = entry->next;
      Deallocate(entry);
    } else {
      link = &entry->next;
    }
  }
}

}  // namespace

ProcessFlagRegistry::~ProcessFlagRegistry() {
  RemoveEntries(this, nullptr, nullptr);
}

ProcessFlagRegistration ProcessFlagRegistry::Register(
    const char* name, const char* filename, const char* type,
    ProcessFlagInfo* previous) {
  SharedRegistry* const registry = GetSharedRegistry();
  if (registry == nullptr) {
    return ProcessFlagRegistration::kUnavailable;
  }
  const SharedRegistryLock lock(*registry);
  for (const SharedEntry* entry = registry->head; entry != nullptr;
       entry = entry->next) {
    if (std::strcmp(entry->name, name) == 0) {
      if (previous != nullptr) {
        *previous = {entry->name, entry->filename, entry->type};
      }
      return ProcessFlagRegistration::kDuplicate;
    }
  }
  SharedEntry* const entry = CreateEntry(this, name, filename, type);
  if (entry == nullptr) {
    return ProcessFlagRegistration::kUnavailable;
  }
  entry->next = registry->head;
  registry->head = entry;
  return ProcessFlagRegistration::kRegistered;
}

void ProcessFlagRegistry::Unregister(const char* name, const char* filename) {
  RemoveEntries(this, name, filename);
}

void LockProcessFlagRegistryForFork() {
  SharedRegistry* const registry = GetSharedRegistry();
  if (registry != nullptr) {
    LockSharedRegistry(*registry);
  }
}

void UnlockProcessFlagRegistryAfterFork() {
  SharedRegistry* const registry = GetSharedRegistry();
  if (registry != nullptr) {
    UnlockSharedRegistry(*registry);
  }
}

}  // namespace internal
}  // namespace nglog
