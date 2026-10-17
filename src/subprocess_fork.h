// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_SUBPROCESS_FORK_H
#define NGLOG_INTERNAL_SUBPROCESS_FORK_H

#include <sys/types.h>

namespace nglog {
namespace internal {

using ForkFunction = pid_t (*)();
using ExecFunction = int (*)(char* const[], char* const[], const char*);

// These hooks are internal implementation details that run in the child and
// use only async-signal-safe operations. ExecuteWithExecv() passes envp to the
// program where execve() exists and ignores the search path.
// ExecuteWithExecvp() also searches the search path for a program without a
// slash in its name, or the default directories of execvp() without one. The
// parent reads the search path before fork(), since getenv() is not
// async-signal-safe.
int ExecuteWithExecv(char* const argv[], char* const envp[],
                     const char* search_path);
int ExecuteWithExecvp(char* const argv[], char* const envp[],
                      const char* search_path);
pid_t SpawnProcessWithFork(char* const argv[], char* const envp[],
                           const char* search_path, int stdin_fd, int stdout_fd,
                           int exec_error_fd, ForkFunction fork_process,
                           ExecFunction exec_process) noexcept;

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_SUBPROCESS_FORK_H
