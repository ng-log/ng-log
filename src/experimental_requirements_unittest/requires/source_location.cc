// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Simulates a standard library without std::source_location. The header must
// reject it with a diagnostic naming the requirement.

#include <version>

#undef __cpp_lib_source_location

#include <ng-log/experimental/logging.h>

int main() {}
