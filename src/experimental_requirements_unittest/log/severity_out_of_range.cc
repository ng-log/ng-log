// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// A severity outside the defined ones must fail to compile during constant
// evaluation with a diagnostic naming the reason.

#include <ng-log/experimental/log.h>

constexpr nglog::experimental::Severity kInvalid{
    static_cast<nglog::LogSeverity>(NGLOG_FATAL + 1)};

int main() { nglog::experimental::Log(kInvalid, "invalid"); }
