// Copyright (c) 2024, Google Inc.
// Copyright (c) 2026, The ng-log contributors
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Author: Satoru Takabayashi
//
// This is a helper binary for testing signalhandler.cc.  The actual test
// is done in signalhandler_unittest.sh.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <sstream>
#include <string>
#include <thread>

#include "config.h"
#include "ng-log/logging.h"
#include "ng-log/platform.h"
#include "stacktrace.h"
#include "symbolize.h"

#if defined(HAVE_UNISTD_H)
#  include <unistd.h>
#endif
#if !defined(NGLOG_OS_WINDOWS)
#  include <sys/wait.h>
#endif
#ifdef NGLOG_USE_GFLAGS
#  include <gflags/gflags.h>
using namespace GFLAGS_NAMESPACE;
#endif
#if defined(_MSC_VER)
#  include <io.h>  // write
#endif

using namespace nglog;

static void DieInThread(int* a) {
  std::ostringstream oss;
  oss << std::showbase << std::hex << std::this_thread::get_id();

  fprintf(stderr, "%s is dying\n", oss.str().c_str());
  int b = 1 / *a;
  fprintf(stderr, "We should have died: b=%d\n", b);
}

static void WriteToStdout(const char* data, size_t size) {
  if (write(fileno(stdout), data, size) < 0) {
    // Ignore errors.
  }
}

// The first time this runs, it crashes again with a different signal
// while the first crash is still being dumped.
static void WriteAndReenter(const char* data, size_t size) {
  if (write(fileno(stderr), data, size) < 0) {
    // Ignore errors.
  }
  static std::atomic_flag entered = ATOMIC_FLAG_INIT;
  if (!entered.test_and_set()) {
    raise(SIGTERM);
  }
}

static void CrashInThread() {
  volatile int* ptr = nullptr;
  *ptr = 0;
}

#if !defined(NGLOG_OS_WINDOWS)
// Runs self_path with child_command as its only argument and checks
// that the child terminates by one of acceptable_signals within
// timeout_seconds. Used to verify the reentrancy guard in
// signalhandler.cc actually lets the process die instead of hanging.
// More than one acceptable signal matters whenever a second signal can
// legitimately fire while the first is still being dumped, whether
// deliberately (the reentrant scenario) or as a side effect of dumping
// itself (some platforms hit an internal abort() while symbolizing a
// std::thread-invoked frame). Once both handler invocations have
// returned, two pending unblocked signals race, and which one the
// kernel ends up delivering first is platform-dependent, not a
// correctness property of the guard. The child's own stdout and stderr
// are left connected to this process's, so a crash dump shows up
// directly in the test log alongside the diagnostics below.
static bool ExpectChildDiesBySignal(
    const char* self_path, const char* child_command,
    std::initializer_list<int> acceptable_signals, int timeout_seconds) {
  pid_t pid = fork();
  if (pid == 0) {
    execl(self_path, self_path, child_command, static_cast<char*>(nullptr));
    _exit(127);
  }
  if (pid < 0) {
    fprintf(stderr, "%s: fork failed\n", child_command);
    return false;
  }

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
  int status = 0;
  bool reaped = false;
  while (std::chrono::steady_clock::now() < deadline) {
    if (waitpid(pid, &status, WNOHANG) == pid) {
      reaped = true;
      break;
    }
    usleep(20000);
  }
  if (!reaped) {
    fprintf(stderr, "%s: timed out after %d seconds\n", child_command,
            timeout_seconds);
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return false;
  }
  if (WIFSIGNALED(status)) {
    for (int signal : acceptable_signals) {
      if (WTERMSIG(status) == signal) {
        return true;
      }
    }
    fprintf(stderr, "%s: died by unexpected signal %d\n", child_command,
            WTERMSIG(status));
  } else if (WIFEXITED(status)) {
    fprintf(stderr, "%s: exited with code %d instead of dying by a signal\n",
            child_command, WEXITSTATUS(status));
  } else {
    fprintf(stderr, "%s: unexpected status 0x%x\n", child_command, status);
  }
  return false;
}
#endif  // !defined(NGLOG_OS_WINDOWS)

int main(int argc, char** argv) {
#if defined(HAVE_STACKTRACE) && defined(HAVE_SYMBOLIZE)
  InitializeLogging(argv[0]);
#  ifdef NGLOG_USE_GFLAGS
  ParseCommandLineFlags(&argc, &argv, true);
#  endif
  InstallFailureSignalHandler();
  const std::string command = argc > 1 ? argv[1] : "none";
  if (command == "segv") {
    // We'll check if this is outputted.
    LOG(INFO) << "create the log file";
    LOG(INFO) << "a message before segv";
    // We assume 0xDEAD is not writable.
    int* a = (int*)0xDEAD;
    *a = 0;
  } else if (command == "loop") {
    fprintf(stderr, "looping\n");
    while (true);
  } else if (command == "die_in_thread") {
    std::thread t{&DieInThread, nullptr};
    t.join();
  } else if (command == "dump_to_stdout") {
    InstallFailureWriter(WriteToStdout);
    abort();
  } else if (command == "installed") {
    fprintf(stderr, "signal handler installed: %s\n",
            IsFailureSignalHandlerInstalled() ? "true" : "false");
  } else if (command == "reentrant") {
    InstallFailureWriter(&WriteAndReenter);
    abort();
  } else if (command == "multithread") {
    std::thread t1{&CrashInThread};
    std::thread t2{&CrashInThread};
    std::thread t3{&CrashInThread};
    std::thread t4{&CrashInThread};
    t1.join();
    t2.join();
    t3.join();
    t4.join();
#  if !defined(NGLOG_OS_WINDOWS)
  } else if (command == "self_test") {
    const bool ok =
        ExpectChildDiesBySignal(argv[0], "segv", {SIGSEGV}, 10) &&
        ExpectChildDiesBySignal(argv[0], "reentrant", {SIGTERM, SIGABRT},
                                10) &&
        ExpectChildDiesBySignal(argv[0], "multithread", {SIGSEGV, SIGABRT},
                                10);
    puts(ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
#  endif  // !defined(NGLOG_OS_WINDOWS)
  } else {
    // Tell the shell script
    puts("OK");
  }
#endif
  return 0;
}
