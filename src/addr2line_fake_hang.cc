// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//
// A stand-in for an unresponsive "addr2line" used by addr2line_unittest.cc:
// ignores its arguments, produces no output, and never exits on its own,
// exercising the timeout/forced-termination path in RunAddr2Line().

#include <chrono>
#include <thread>

int main() {
  for (;;) {
    std::this_thread::sleep_for(std::chrono::hours{1});
  }
}
