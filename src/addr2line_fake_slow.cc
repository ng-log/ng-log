// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//
// A stand-in for an addr2line process that emits output in small chunks. It
// checks that one addr2line request has one shared timeout.

#include <chrono>
#include <cstdio>
#include <thread>

namespace {
constexpr std::chrono::milliseconds kChunkDelay{20};
constexpr char kOutput[] = "slow\n??:0\n";
}  // namespace

int main() {
  for (const char character : kOutput) {
    if (character == '\0') {
      break;
    }

    std::fwrite(&character, 1, 1, stdout);
    std::fflush(stdout);
    std::this_thread::sleep_for(kChunkDelay);
  }

  return 0;
}
