// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Composing two destinations of the same category must name the reason it is
// rejected.

#include <ng-log/experimental/log.h>

namespace ngx = nglog::experimental;

int main() {
  ngx::Log(ngx::Info | ngx::Syslog | ngx::ToString{nullptr}, "message");
}
