// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include <cstdio>
#include <cstdlib>

int main() {
  std::fputs("failed\nfile.cc:1\n", stdout);
  return EXIT_FAILURE;
}
