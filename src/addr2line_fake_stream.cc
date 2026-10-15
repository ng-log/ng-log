// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//
// A stand-in for an addr2line process that keeps emitting output in small
// chunks and never exits on its own. Every chunk arrives well within the
// timeout of a single read, so only a timeout shared by the whole addr2line
// request can stop it.

#include <chrono>
#include <cstdio>
#include <thread>

namespace {
constexpr std::chrono::milliseconds kChunkDelay{20};
constexpr char kChunk = '?';
}  // namespace

int main() {
  for (;;) {
    std::fputc(kChunk, stdout);
    std::fflush(stdout);
    std::this_thread::sleep_for(kChunkDelay);
  }
}
