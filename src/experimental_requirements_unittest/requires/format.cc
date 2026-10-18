// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Simulates a standard library whose std::format lacks compile-time checked
// format strings. The header must reject it with a diagnostic naming the
// requirement.

#include <version>

#undef __cpp_lib_format
#define __cpp_lib_format 201907L

#include <ng-log/experimental/logging.h>

int main() {}
