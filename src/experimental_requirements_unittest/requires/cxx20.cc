// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Including the experimental logging functions before C++20 must fail with a
// diagnostic naming the requirement.

#include <ng-log/experimental/logging.h>

int main() {}
