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

#include <cstdio>
#include <cstring>
#include <thread>

namespace {
constexpr std::size_t kBufferSize = 4096;
}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--hang") == 0) {
    for (;;) {
      std::this_thread::sleep_for(std::chrono::hours{1});
    }
  }

  char buf[kBufferSize];
  std::size_t bytes_read;

  while ((bytes_read = std::fread(buf, 1, sizeof(buf), stdin)) > 0) {
    std::fwrite(buf, 1, bytes_read, stdout);
    std::fflush(stdout);
  }

  return 0;
}
