// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// That() must not compare wide C strings by address.

#include <ng-log/experimental/ensure.h>
#include <ng-log/experimental/log.h>

int main() {
  const wchar_t* const s = L"abc";
  nglog::experimental::Ensure(nglog::experimental::That(s) == L"abc");
}
