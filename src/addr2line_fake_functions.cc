// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//
// A stand-in for addr2line that emits a resolved function and source line.
// It lets the MinGW symbolization tests exercise output formatting without
// depending on a Windows addr2line installation.

#include <cstdio>

int main() {
  std::fputs("main\nfake.cc:42\n", stdout);
  return 0;
}
