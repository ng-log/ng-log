// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//
// A tiny, dependency-free companion program for subprocess_unittest.cc. With no
// arguments, copies stdin to stdout until EOF. With "--hang", never exits on
// its own, to exercise Subprocess::Wait()'s forced termination. Kept
// independent of ng-log/the platform port so it builds identically, without
// relying on a shell or OS-shipped utility being present, on every platform
// Subprocess itself supports.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#endif

namespace {
constexpr std::size_t kBufferSize = 4096;
}  // namespace

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

  char buf[kBufferSize];
  std::size_t bytes_read;

  while ((bytes_read = std::fread(buf, 1, sizeof(buf), stdin)) > 0) {
    std::fwrite(buf, 1, bytes_read, stdout);
    std::fflush(stdout);
  }

  return 0;
}

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
