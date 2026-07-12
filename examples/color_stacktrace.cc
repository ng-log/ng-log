// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//
// Demonstrates the coloring support added to both the regular logging path and
// the crash handler's stack traces. Run this in a real terminal to see it:
// colorizing auto-detects whether that's safe and stays off otherwise (e.g.
// when output is piped or redirected to a file).

#include <ng-log/flags.h>
#include <ng-log/logging.h>

#include <cerrno>
#include <cstdlib>

#ifdef __unix__
#  include <sys/resource.h>
#endif  // __unix__

namespace {

// Crashing on purpose, below, would otherwise leave the OS to write out a
// core dump before the process actually exits, depending on how the host
// is configured (a core_pattern piped to a slow or unavailable collector,
// e.g. systemd-coredump in some sandboxed/containerized environments)
// that can take a long time or appear to hang. This example cares about
// the crash handler's own output, not the core dump, so it disables core
// dumps outright to make the process exit promptly everywhere.
void DisableCoreDumps() {
#ifdef __unix__
  const struct rlimit no_core = {0, 0};
  setrlimit(RLIMIT_CORE, &no_core);
#endif  // __unix__
}

// Recurses a few times before crashing so the stack trace below has more
// than one frame to show off file:line/function coloring on.
void CrashThroughFewFrames(int depth);

void CrashThroughFewFramesIndirectly(int depth) {
  CrashThroughFewFrames(depth);
}

void CrashThroughFewFrames(int depth) {
  if (depth > 0) {
    CrashThroughFewFramesIndirectly(depth - 1);
    return;
  }
  // LOG(FATAL) shows the FATAL severity's own coloring on its log line,
  // then aborts, which InstallFailureSignalHandler() (installed by the
  // caller, along with routing the abort through a real, catchable
  // signal below) turns into the colorized stack trace below.
  LOG(FATAL) << "fatal messages are colored bold red, then abort";
}

}  // namespace

int main(int /*argc*/, char** argv) {
  DisableCoreDumps();
  nglog::InitializeLogging(argv[0]);

  // Both default to true: colorizing is opt-out, not opt-in. Uncomment
  // either line to see the difference.
  // FLAGS_colorlogtostderr = false;
  // FLAGS_symbolize_hyperlinks = false;

  // FLAGS_stderrthreshold defaults to ERROR, so INFO/WARNING messages
  // would otherwise go only to a log file, never to the terminal this
  // example colorizes.
  FLAGS_logtostderr = true;

  // Lets the VLOG(1) call below actually produce output.
  FLAGS_v = 1;

  // Regular log lines are colorized by severity when writing to a
  // terminal: INFO is left uncolored, WARNING is yellow, ERROR is red,
  // and FATAL is bold red. This coloring is applied uniformly across
  // every macro that eventually constructs a LogMessage, not just LOG()
  // itself.
  LOG(INFO) << "colorized logging is on by default on a real terminal";
  LOG(WARNING) << "warnings stand out in yellow";
  LOG(ERROR) << "errors stand out in red";
  VLOG(1) << "verbose messages share LOG(INFO)'s coloring";
  DLOG(INFO) << "debug-only messages share LOG(INFO)'s coloring";
  DLOG(WARNING) << "debug-only messages share LOG(WARNING)'s coloring";
  DLOG(ERROR) << "debug-only messages share LOG(ERROR)'s coloring";
  errno = ENOENT;
  PLOG(WARNING) << "errno-appended messages stay colored consistently";
  CHECK(1 + 1 == 2) << "a passing CHECK produces no output at all";

  // A crash's stack trace colors the address, file:line, and function
  // name of each frame separately, and (terminal permitting) turns the
  // file:line reference into a clickable OSC 8 hyperlink back to the
  // source. FLAGS_symbolize_file_base_path can be set to resolve source
  // paths that debug info recorded as relative to the build directory.
  nglog::InstallFailureSignalHandler();

  // LOG(FATAL) defaults to its own separate, uncolored stack-trace-and-
  // exit path, bypassing the crash handler installed above, routing it
  // through plain abort() instead raises a real, catchable signal, so it
  // goes through the same colorized crash banner as any other crash.
  nglog::InstallFailureFunction(
      reinterpret_cast<nglog::logging_fail_func_t>(&std::abort));

  LOG(INFO) << "crashing on purpose to show a colorized stack trace";
  CrashThroughFewFrames(3);
}
