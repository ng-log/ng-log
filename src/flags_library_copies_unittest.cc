// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Loads separate copies of ng-log into one process. This executable does not
// link ng-log itself, so every copy comes from a loaded module.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <dlfcn.h>
#  include <fcntl.h>
#  include <spawn.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <sys/syscall.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>

extern char** environ;
#endif

namespace {

constexpr char kLoadCopyOption[] = "--load-library-copy";
constexpr std::int32_t kFirstCopyValue = 1;
constexpr std::int32_t kSecondCopyValue = 2;
constexpr int kLoadFailure = 2;
constexpr int kStillLoaded = 3;
constexpr int kUnexpectedValue = 4;

#ifdef _WIN32
using Library = HMODULE;
#else
using Library = void*;
#endif

using ValueFunction = std::int32_t (*)();

Library Load(const char* path) {
#ifdef _WIN32
  return LoadLibraryA(path);
#else
  return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

void Unload(Library library) {
#ifdef _WIN32
  FreeLibrary(library);
#else
  dlclose(library);
#endif
}

bool IsLoaded(const char* path) {
#ifdef _WIN32
  return GetModuleHandleA(path) != nullptr;
#else
  void* const library = dlopen(path, RTLD_NOW | RTLD_NOLOAD);
  if (library == nullptr) {
    return false;
  }
  dlclose(library);
  return true;
#endif
}

std::int32_t Value(Library library) {
#ifdef _WIN32
  const auto function = reinterpret_cast<ValueFunction>(
      reinterpret_cast<void*>(GetProcAddress(library, "LibraryCopyValue")));
#else
  const auto function =
      reinterpret_cast<ValueFunction>(dlsym(library, "LibraryCopyValue"));
#endif
  return function == nullptr ? 0 : function();
}

// Returns every variable of the environment shared by all libraries in the
// process, sorted to compare snapshots.
std::vector<std::string> EnvironmentSnapshot() {
  std::vector<std::string> variables;
#ifdef _WIN32
  char* const block = GetEnvironmentStringsA();
  for (const char* variable = block; *variable != '\0';
       variable += std::strlen(variable) + 1) {
    variables.emplace_back(variable);
  }
  FreeEnvironmentStringsA(block);
#else
  for (char** variable = environ; *variable != nullptr; ++variable) {
    variables.emplace_back(*variable);
  }
#endif
  std::sort(variables.begin(), variables.end());
  return variables;
}

void ClearEnvironment() {
  for (const std::string& variable : EnvironmentSnapshot()) {
    const std::string name = variable.substr(0, variable.find('='));
    if (name.empty()) {
      continue;
    }
#ifdef _WIN32
    SetEnvironmentVariableA(name.c_str(), nullptr);
#else
    unsetenv(name.c_str());
#endif
  }
}

// Starts this executable again with the given module to load. The child
// inherits the environment of this process.
int RunChild(const char* library) {
#ifdef _WIN32
  char executable[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, executable, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return -1;
  }
  std::string command = std::string("\"") + executable + "\" " +
                        kLoadCopyOption + " \"" + library + "\"";
  STARTUPINFOA startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessA(nullptr, &command[0], nullptr, nullptr, FALSE, 0, nullptr,
                      nullptr, &startup, &process)) {
    return -1;
  }
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return static_cast<int>(code);
#else
  std::string executable = "/proc/self/exe";
  std::string option = kLoadCopyOption;
  std::string path = library;
  char* const argv[] = {&executable[0], &option[0], &path[0], nullptr};
  pid_t pid = 0;
  if (posix_spawn(&pid, executable.c_str(), nullptr, nullptr, argv, environ) !=
      0) {
    return -1;
  }
  int status = 0;
  if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) {
    return -1;
  }
  return WEXITSTATUS(status);
#endif
}

// Loads a copy inside a death test child and ends the child with the load
// failure status if that fails.
Library LoadOrExit(const char* path) {
  const Library library = Load(path);
  if (library == nullptr) {
    std::exit(kLoadFailure);
  }
  return library;
}

// Ends a death test child successfully if the first copy loads and reports
// its own value.
[[noreturn]] void ExitWithFirstCopyStatus() {
  const Library first = Load(NGLOG_LIBRARY_COPY_FIRST);
  std::exit(first != nullptr && Value(first) == kFirstCopyValue ? EXIT_SUCCESS
                                                                : kLoadFailure);
}

int LoadCopyInChild(const char* path) {
  const Library library = Load(path);
  if (library == nullptr) {
    return kLoadFailure;
  }
  return Value(library) == kSecondCopyValue ? EXIT_SUCCESS : kUnexpectedValue;
}

TEST(FlagLibraryCopies, DuplicateDefinitionsAcrossCopiesAreFatal) {
  EXPECT_DEATH(
      {
        LoadOrExit(NGLOG_LIBRARY_COPY_FIRST);
        Load(NGLOG_LIBRARY_COPY_SECOND);
      },
      "defined more than once"
      // Every line is matched separately because the simple regular
      // expressions on Windows do not match line breaks with a period.
      ".*\n.*First:.*\n.*Second:"
      ".*\n.*Reason:.*Another copy of ng-log in this process already"
      ".*\n.*option\\. This happens when several libraries each link"
      ".*\n.*statically\\."
      ".*\n.*Hint:.*Link all libraries and the executable against a single");
}

TEST(FlagLibraryCopies, DetectionDoesNotDependOnTheEnvironment) {
  EXPECT_DEATH(
      {
        LoadOrExit(NGLOG_LIBRARY_COPY_FIRST);
        ClearEnvironment();
        Load(NGLOG_LIBRARY_COPY_SECOND);
      },
      "defined more than once");
}

#if defined(__linux__)
// Publishes an anchor whose registry address points to memory that is not a
// registry, as corrupted memory would.
void PublishCorruptedAnchor() {
  static std::uint64_t not_a_registry[4] = {};
  const long fd = syscall(SYS_memfd_create, "nglog-process-flag-registry-v2",
                          1U);  // MFD_CLOEXEC
  if (fd < 0 || ftruncate(static_cast<int>(fd), sizeof(std::uintptr_t)) != 0) {
    std::exit(kLoadFailure);
  }
  void* const memory =
      mmap(nullptr, sizeof(std::uintptr_t), PROT_READ | PROT_WRITE, MAP_PRIVATE,
           static_cast<int>(fd), 0);
  if (memory == MAP_FAILED) {
    std::exit(kLoadFailure);
  }
  *static_cast<std::uintptr_t*>(memory) =
      reinterpret_cast<std::uintptr_t>(not_a_registry);
}

// Closes the descriptors of ng-log memory files, as a program that closes
// descriptors it does not know would. Other descriptors stay open because the
// test framework uses them.
void CloseRegistryDescriptors() {
  constexpr int kMaximumDescriptor = 1024;
  constexpr std::size_t kTargetSize = 256;
  for (int fd = 0; fd < kMaximumDescriptor; ++fd) {
    const std::string path = "/proc/self/fd/" + std::to_string(fd);
    char target[kTargetSize];
    const ssize_t length = readlink(path.c_str(), target, sizeof(target) - 1);
    if (length > 0) {
      target[length] = '\0';
      if (std::strstr(target, "nglog-process-flag-registry") != nullptr) {
        close(fd);
      }
    }
  }
}

TEST(FlagLibraryCopies, DetectionSurvivesClosedDescriptors) {
  EXPECT_DEATH(
      {
        LoadOrExit(NGLOG_LIBRARY_COPY_FIRST);
        CloseRegistryDescriptors();
        Load(NGLOG_LIBRARY_COPY_SECOND);
      },
      "defined more than once");
}

// Maps a regular file whose path contains the name of the anchor. Its content
// is the aligned address of an inaccessible page, which passes the validation
// of an anchor on Linux. Using the file as the anchor would therefore read a
// registry at that address and crash.
void MapLookalikeFile() {
  char directory[] = "/tmp/nglog-lookalike-XXXXXX";
  if (mkdtemp(directory) == nullptr) {
    std::exit(kLoadFailure);
  }
  const std::string path =
      std::string(directory) + "/memfd:nglog-process-flag-registry-v2";
  const int fd = open(path.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
  if (fd < 0 || ftruncate(fd, sizeof(std::uintptr_t)) != 0) {
    std::exit(kLoadFailure);
  }
  void* const memory = mmap(nullptr, sizeof(std::uintptr_t),
                            PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  close(fd);
  const auto page_size = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
  void* const inaccessible =
      mmap(nullptr, page_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (memory != MAP_FAILED && inaccessible != MAP_FAILED) {
    *static_cast<std::uintptr_t*>(memory) =
        reinterpret_cast<std::uintptr_t>(inaccessible);
  }
  unlink(path.c_str());
  rmdir(directory);
  if (memory == MAP_FAILED || inaccessible == MAP_FAILED) {
    std::exit(kLoadFailure);
  }
}

TEST(FlagLibraryCopies, MappedFilesThatResembleTheAnchorAreIgnored) {
  EXPECT_EXIT(
      {
        MapLookalikeFile();
        ExitWithFirstCopyStatus();
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST(FlagLibraryCopies, CorruptedAnchorFallsBackToAPrivateRegistry) {
  EXPECT_EXIT(
      {
        PublishCorruptedAnchor();
        ExitWithFirstCopyStatus();
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
}
#endif

#ifdef _WIN32
// Creates the named anchor of this process before any copy of ng-log does, as
// another process in the same session could, and stores an address that is
// not valid in it.
void PlantForeignAnchor() {
  FILETIME creation_time;
  FILETIME exit_time;
  FILETIME kernel_time;
  FILETIME user_time;
  if (!GetProcessTimes(GetCurrentProcess(), &creation_time, &exit_time,
                       &kernel_time, &user_time)) {
    std::exit(kLoadFailure);
  }
  const std::string name = "Local\\nglog-process-flag-registry-v2-" +
                           std::to_string(GetCurrentProcessId()) + "-" +
                           std::to_string(creation_time.dwHighDateTime) + "-" +
                           std::to_string(creation_time.dwLowDateTime);
  const HANDLE mapping =
      CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                         sizeof(std::uintptr_t), name.c_str());
  if (mapping == nullptr) {
    std::exit(kLoadFailure);
  }
  void* const memory =
      MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(std::uintptr_t));
  if (memory == nullptr) {
    std::exit(kLoadFailure);
  }
  constexpr std::uintptr_t kInvalidAddress = 1;
  *static_cast<std::uintptr_t*>(memory) = kInvalidAddress;
}

TEST(FlagLibraryCopies, ForeignAnchorFallsBackToAPrivateRegistry) {
  EXPECT_EXIT(
      {
        PlantForeignAnchor();
        ExitWithFirstCopyStatus();
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
}
#endif

TEST(FlagLibraryCopies, LoadingACopyLeavesTheEnvironmentUnchanged) {
  EXPECT_EXIT(
      {
        const std::vector<std::string> before = EnvironmentSnapshot();
        LoadOrExit(NGLOG_LIBRARY_COPY_FIRST);
        std::exit(EnvironmentSnapshot() == before ? EXIT_SUCCESS
                                                  : EXIT_FAILURE);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST(FlagLibraryCopies, UnloadedCopyReleasesItsFlags) {
  EXPECT_EXIT(
      {
        const Library first = LoadOrExit(NGLOG_LIBRARY_COPY_FIRST);
        if (Value(first) != kFirstCopyValue) {
          std::exit(kUnexpectedValue);
        }
        Unload(first);
        if (IsLoaded(NGLOG_LIBRARY_COPY_FIRST)) {
          std::exit(kStillLoaded);
        }
        const Library second = LoadOrExit(NGLOG_LIBRARY_COPY_SECOND);
        std::exit(Value(second) == kSecondCopyValue ? EXIT_SUCCESS
                                                    : kUnexpectedValue);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST(FlagLibraryCopies, ChildProcessesUseTheirOwnRegistry) {
  const Library first = Load(NGLOG_LIBRARY_COPY_FIRST);
  ASSERT_NE(nullptr, first);
  EXPECT_EQ(EXIT_SUCCESS, RunChild(NGLOG_LIBRARY_COPY_SECOND));
  Unload(first);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 3 && std::strcmp(argv[1], kLoadCopyOption) == 0) {
    return LoadCopyInChild(argv[2]);
  }
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
