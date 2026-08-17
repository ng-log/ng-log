// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_SUBPROCESS_FORK_H
#define NGLOG_INTERNAL_SUBPROCESS_FORK_H

#include <sys/types.h>

namespace nglog {
namespace internal {

using ForkFunction = pid_t (*)();
using ExecFunction = int (*)(char* const[], char* const[]);

// These hooks are internal implementation details. When called in the child,
// both functions must use only async-signal-safe operations.
int ExecuteWithExecv(char* const argv[], char* const envp[]);
int ExecuteWithExecvp(char* const argv[], char* const envp[]);
pid_t SpawnProcessWithFork(char* const argv[], char* const envp[], int stdin_fd,
                           int stdout_fd, int exec_error_fd,
                           ForkFunction fork_process,
                           ExecFunction exec_process) noexcept;

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_SUBPROCESS_FORK_H
