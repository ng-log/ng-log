// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// ng-log/flags.h declares the built-in flags with the linkage of the ng-log
// library. Declaring one again must not compile, because the declaration
// would disagree with that linkage when ng-log is a shared library.

#include <ng-log/flags.h>

NGLOG_DECLARE(bool, logtostderr);

int main() { return nglog::GetFlag(FLAGS_logtostderr) ? 1 : 0; }
