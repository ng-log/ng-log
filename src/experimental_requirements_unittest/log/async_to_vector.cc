// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Appending the text of an asynchronous message to a vector must be rejected,
// since the caller reads the vector before the logging thread appends to it.

#include <ng-log/experimental/log.h>

#include <string>
#include <vector>

namespace ngx = nglog::experimental;

int main() {
  std::vector<std::string> texts;
  ngx::Log(ngx::Async | ngx::ToVector{&texts}, "message");
}
