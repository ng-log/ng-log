// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Tejas Anil Nagmote

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "base/commandlineflags.h"
#include "ng-log/logging.h"
#include "testing_utilities.h"

#ifdef NGLOG_USE_GFLAGS
#  include <gflags/gflags.h>
using namespace GFLAGS_NAMESPACE;
#endif

using namespace nglog;

namespace {

constexpr char kInfoMessage[] = "buffered info message";
constexpr char kWarningMessage[] = "unbuffered warning message";

// Restores the flags this test changes even if it exits early.
struct StdoutFlagSaver {
  ~StdoutFlagSaver() {
    FLAGS_logtostdout = logtostdout_;
    FLAGS_colorlogtostdout = colorlogtostdout_;
    FLAGS_logbuflevel = logbuflevel_;
  }

  bool logtostdout_{FLAGS_logtostdout};
  bool colorlogtostdout_{FLAGS_colorlogtostdout};
  decltype(FLAGS_logbuflevel) logbuflevel_{FLAGS_logbuflevel};
};

// Reads what has actually reached the capture file so far. Unlike
// GetCapturedTestStdout(), this does not stop the capture and therefore does
// not fflush(3) the stream itself, which is what makes it possible to observe
// whether ng-log flushed stdout on its own.
std::string PeekCapturedStdout(const CapturedStream& capture) {
  std::ifstream file{capture.filename(), std::ios::binary};
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

}  // namespace

// Regression test: with stdout fully buffered, a message whose severity
// exceeds --logbuflevel must be flushed by ng-log itself instead of lingering
// in the stdio buffer.
//
// This covers the uncolored exit of ColoredWriteToStderrOrStdout(). The
// colored exit cannot be covered from here: it is only reached when
// LogDestination::terminal_supports_color() is true, and that is a namespace
// scope static initialized from TERM before main() runs, so the test cannot
// select it. Where it is reached, the Windows branch flushes unconditionally
// anyway, so only the ANSI branch depends on the flush being applied.
TEST(LogToStdout, HonorsLogbuflevel) {
  const StdoutFlagSaver flag_saver;

  std::string after_info;
  std::string after_warning;

  {
    CapturedStream capture{fileno(stdout), TestTmpDir() + "/logtostdout.out"};

    // stdout now refers to a regular file; make sure it is fully buffered so
    // that messages which are not explicitly flushed stay in the stdio buffer.
    setvbuf(stdout, nullptr, _IOFBF, BUFSIZ);

    FLAGS_logtostdout = true;
    FLAGS_colorlogtostdout = false;
    FLAGS_logbuflevel = NGLOG_INFO;  // buffer INFO, flush WARNING and above

    LOG(INFO) << kInfoMessage;
    after_info = PeekCapturedStdout(capture);

    LOG(WARNING) << kWarningMessage;
    after_warning = PeekCapturedStdout(capture);

    capture.StopCapture();
  }

  // INFO does not exceed --logbuflevel, so it stays in the stdio buffer.
  EXPECT_THAT(after_info, testing::Not(testing::HasSubstr(kInfoMessage)));

  // WARNING does, so it must have been written through. Flushing the stream
  // also emits everything buffered before it.
  EXPECT_THAT(after_warning, testing::HasSubstr(kWarningMessage));
  EXPECT_THAT(after_warning, testing::HasSubstr(kInfoMessage));
}

int main(int argc, char** argv) {
  FLAGS_colorlogtostderr = false;

  // Make sure stderr is not buffered as stderr seems to be buffered
  // on recent windows.
  setvbuf(stderr, nullptr, _IONBF, 0);

  testing::InitGoogleTest(&argc, argv);
  testing::InitGoogleMock(&argc, argv);

#ifdef NGLOG_USE_GFLAGS
  ParseCommandLineFlags(&argc, &argv, true);
#endif

  return RUN_ALL_TESTS();
}
