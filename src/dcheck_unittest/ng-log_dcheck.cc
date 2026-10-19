// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include <ng-log/flags.h>
#include <ng-log/logging.h>

int main(int /*argc*/, char** argv) {
  // The test inspects the plain output of every severity on stderr. Colors
  // would interleave escape sequences with the matched text where the output
  // counts as a terminal, such as under Node.js.
  nglog::SetFlag(FLAGS_logtostderr, true);
  nglog::SetFlag(FLAGS_colorlogtostderr, false);
  nglog::InitializeLogging(argv[0]);
  nglog::InstallFailureSignalHandler();

#if defined(_MSC_VER)
  // Avoid presenting an interactive dialog that will cause the test to time
  // out.
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif  // defined(_MSC_VER)

  DLOG(INFO) << "no output";
  DLOG(WARNING) << "no output";
  DLOG(ERROR) << "no output";

  // Must not fail in release build
  DLOG_ASSERT(false);

  // Must be the last expression
  DLOG(FATAL) << "no output";
}
