// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//
// A tiny, dependency-free companion program for subprocess_unittest.cc. With no
// arguments, copies stdin to stdout until EOF. With "--hang", never exits on
// its own, to exercise Subprocess::Wait()'s forced termination. With
// "--echo-arg", writes the following argument to stdout. With "--echo-env",
// writes the value of the named environment variable. With "--fail", exits
// unsuccessfully. On POSIX systems, "--list-fds" lists the inherited
// descriptors besides the standard streams and "--check-stderr" reports
// whether the standard error stream is open. On Windows, "--check-handle"
// reports whether the given handle is an inherited pipe. Kept independent of
// ng-log and the platform port so it builds identically, without relying on a
// shell or OS-shipped utility being present, on every platform Subprocess
// itself supports.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#ifdef _WIN32
#  include <windows.h>

#  include <cstdint>
#  include <string>
#  include <utility>
#  include <vector>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace {

constexpr std::size_t kBufferSize = 4096;

int Run(int argc, const char* const* argv) {
  if (argc > 1 && std::strcmp(argv[1], "--hang") == 0) {
    for (;;) {
      std::this_thread::sleep_for(std::chrono::hours{1});
    }
  }

  if (argc == 3 && std::strcmp(argv[1], "--echo-arg") == 0) {
    std::fwrite(argv[2], 1, std::strlen(argv[2]), stdout);
    return 0;
  }

  if (argc > 1 && std::strcmp(argv[1], "--fail") == 0) {
    return 1;
  }

#ifdef _WIN32
  if (argc == 3 && std::strcmp(argv[1], "--check-handle") == 0) {
    // Reports whether the hexadecimal handle value names a pipe that the
    // helper inherited.
    constexpr int kHexadecimalBase = 16;
    const auto handle = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(
        std::strtoull(argv[2], nullptr, kHexadecimalBase)));
    DWORD flags = 0;
    const bool inherited = GetHandleInformation(handle, &flags) != 0 &&
                           GetFileType(handle) == FILE_TYPE_PIPE;
    std::fputs(inherited ? "inherited" : "clean", stdout);
    return 0;
  }
#else
  if (argc == 2 && std::strcmp(argv[1], "--list-fds") == 0) {
    // Lists the descriptors besides the standard streams that the helper
    // inherited.
    constexpr int kHighestListedDescriptor = 256;
    for (int fd = STDERR_FILENO + 1; fd <= kHighestListedDescriptor; ++fd) {
      if (fcntl(fd, F_GETFD) != -1) {
        std::printf("%d\n", fd);
      }
    }
    return 0;
  }

  if (argc == 2 && std::strcmp(argv[1], "--check-stderr") == 0) {
    // Reports whether the helper has a standard error stream.
    std::fputs(fcntl(STDERR_FILENO, F_GETFD) != -1 ? "open" : "closed", stdout);
    return 0;
  }
#endif

  if (argc == 3 && std::strcmp(argv[1], "--echo-env") == 0) {
#ifdef _WIN32
    // The narrow environment uses the ANSI code page, so the value is read
    // in UTF-16 and written as UTF-8.
    std::wstring name(argv[2], argv[2] + std::strlen(argv[2]));
    const wchar_t* const value = _wgetenv(name.c_str());
    if (value == nullptr) {
      return 1;
    }
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value,
                                         -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
      return 1;
    }
    std::string utf8(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, &utf8[0],
                            size, nullptr, nullptr) <= 0) {
      return 1;
    }
    std::fwrite(utf8.data(), 1, utf8.size() - 1, stdout);
#else
    const char* const value = std::getenv(argv[2]);
    if (value == nullptr) {
      return 1;
    }
    std::fwrite(value, 1, std::strlen(value), stdout);
#endif
    return 0;
  }

  char buf[kBufferSize];
  std::size_t bytes_read;

  while ((bytes_read = std::fread(buf, 1, sizeof(buf), stdin)) > 0) {
    std::fwrite(buf, 1, bytes_read, stdout);
    std::fflush(stdout);
  }

  return 0;
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
  std::vector<std::string> utf8_args;
  utf8_args.reserve(static_cast<std::size_t>(argc));
  for (int index = 0; index < argc; ++index) {
    const int size =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[index], -1,
                            nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
      return 1;
    }
    std::string argument(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[index], -1,
                            &argument[0], size, nullptr, nullptr) <= 0) {
      return 1;
    }
    argument.resize(static_cast<std::size_t>(size - 1));
    utf8_args.push_back(std::move(argument));
  }

  std::vector<const char*> arguments;
  arguments.reserve(utf8_args.size());
  for (const std::string& argument : utf8_args) {
    arguments.push_back(argument.c_str());
  }
  return Run(argc, arguments.data());
}
#else
int main(int argc, char** argv) { return Run(argc, argv); }
#endif
