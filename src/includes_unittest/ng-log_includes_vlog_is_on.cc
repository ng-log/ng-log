// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include <ng-log/vlog_is_on.h>

namespace {

// The macro must not shadow names of the calling code.
bool IsOn(int level) { return VLOG_IS_ON(level); }

}  // namespace

int main() { return IsOn(0) ? 0 : 1; }
