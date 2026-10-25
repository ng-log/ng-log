// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Storing the text of an asynchronous message in a string must be rejected,
// since the caller reads the string before the logging thread writes it.

#include <ng-log/experimental/log.h>

#include <string>

namespace ngx = nglog::experimental;

int main() {
  std::string text;
  ngx::Log(ngx::Async | ngx::ToString{&text}, "message");
}
