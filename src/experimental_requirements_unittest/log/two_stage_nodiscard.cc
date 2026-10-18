// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Discarding the logger of a two-stage call without the second stage must be
// diagnosed, since nothing would be logged.

#include <ng-log/experimental/log.h>

namespace ngx = nglog::experimental;

// The composition cannot be parsed as a declaration.
int main() { ngx::Log(ngx::Error | ngx::Errno); }
