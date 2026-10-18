// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Skipping the log file without a sink must be rejected, since the message
// would not be logged anywhere.

#include <ng-log/experimental/log.h>

namespace ngx = nglog::experimental;

int main() { ngx::Log(ngx::Warning | ngx::SkipLogFile, "message"); }
