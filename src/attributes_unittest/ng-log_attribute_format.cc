// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include <ng-log/platform.h>

NGLOG_ATTRIBUTE_FORMAT(printf, 1, 2)
void Format(const char* /*format*/, ...) {}

int main() {
  // Must not compile: the format expects an integer but receives a string.
  Format("%d", "text");
}
