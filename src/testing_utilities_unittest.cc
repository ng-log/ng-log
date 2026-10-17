// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause

#include "testing_utilities.h"

#include <gmock/gmock.h>
#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "config.h"
#include "ng-log/platform.h"
#include "stacktrace.h"

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#ifndef NGLOG_OS_WINDOWS
#  include <fcntl.h>
#endif

namespace {

using nglog::HasCutOffStackDump;
using nglog::kStackDumpCutOffMarker;
using nglog::ScopedStreamCapture;
using nglog::StandardStream;
using nglog::WriteCompletely;
using testing::Not;

constexpr char kInnerOutput[] = "inner";
constexpr char kOuterOutput[] = "outer";

TEST(ScopedStreamCapture, CapturesWritesToTheStream) {
  ScopedStreamCapture capture{StandardStream::kStderr};
  std::fputs("captured", stderr);
  EXPECT_EQ(capture.Stop(), "captured");
}

TEST(ScopedStreamCapture, CapturesStdoutSeparately) {
  ScopedStreamCapture capture{StandardStream::kStdout};
  std::fputs("to stdout", stdout);
  EXPECT_EQ(capture.Stop(), "to stdout");
}

TEST(ScopedStreamCapture, NestedCaptureRestoresTheOuterCapture) {
  ScopedStreamCapture outer{StandardStream::kStderr};
  {
    ScopedStreamCapture inner{StandardStream::kStderr};
    std::fputs(kInnerOutput, stderr);
    EXPECT_EQ(inner.Stop(), kInnerOutput);
  }
  std::fputs(kOuterOutput, stderr);
  EXPECT_EQ(outer.Stop(), kOuterOutput);
}

void CaptureAndLeaveWithoutStopping() {
  ScopedStreamCapture capture{StandardStream::kStderr};
  std::fputs(kInnerOutput, stderr);
}

TEST(ScopedStreamCapture, LeavingTheScopeRestoresTheStream) {
  ScopedStreamCapture outer{StandardStream::kStderr};
  CaptureAndLeaveWithoutStopping();
  std::fputs(kOuterOutput, stderr);
  EXPECT_EQ(outer.Stop(), kOuterOutput);
}

TEST(ScopedStreamCapture, StoppingTwiceReturnsTheSameOutput) {
  constexpr char kOutput[] = "once";
  ScopedStreamCapture capture{StandardStream::kStderr};
  std::fputs(kOutput, stderr);
  const std::string output = capture.Stop();
  EXPECT_EQ(output, kOutput);
  EXPECT_EQ(capture.Stop(), output);
}

TEST(WriteCompletely, WritesTheData) {
  constexpr char kData[] = "written";
  ScopedStreamCapture capture{StandardStream::kStderr};
  EXPECT_TRUE(WriteCompletely(fileno(stderr), kData, sizeof(kData) - 1));
  EXPECT_EQ(capture.Stop(), kData);
}

TEST(WriteCompletely, FailsForAnInvalidFileDescriptor) {
  constexpr char kData[] = "lost";
  constexpr int kInvalidFileDescriptor = -1;
  EXPECT_FALSE(
      WriteCompletely(kInvalidFileDescriptor, kData, sizeof(kData) - 1));
}

#ifndef NGLOG_OS_WINDOWS
TEST(WriteCompletely, FailsIfOnlyPartOfTheDataFits) {
  int pipe_ends[2];
  ASSERT_EQ(pipe(pipe_ends), 0);
  const nglog::FileDescriptor read_end{pipe_ends[0]};
  const nglog::FileDescriptor write_end{pipe_ends[1]};
  ASSERT_NE(fcntl(write_end.get(), F_SETFL, O_NONBLOCK), -1);
  ASSERT_NE(fcntl(read_end.get(), F_SETFL, O_NONBLOCK), -1);

  // Filling the pipe until it refuses more data measures its capacity. Pipes
  // of Linux hold at most 1 MiB by default, whereas those of Emscripten grow
  // without bound.
  constexpr std::size_t kChunkSize = 4096;
  constexpr std::size_t kMaxCapacity = std::size_t{16} << 20;
  const std::vector<char> chunk(kChunkSize, 'x');
  std::size_t capacity = 0;
  for (bool full = false; !full;) {
    if (capacity >= kMaxCapacity) {
      GTEST_SKIP() << "the pipe took " << capacity
                   << " bytes without refusing more";
    }
    const ssize_t written = write(write_end.get(), chunk.data(), chunk.size());
    if (written < 0) {
      ASSERT_EQ(errno, EAGAIN) << "errno " << errno;
      full = true;
    } else {
      capacity += static_cast<std::size_t>(written);
    }
  }
  std::vector<char> drained(capacity);
  ASSERT_EQ(read(read_end.get(), drained.data(), drained.size()),
            static_cast<ssize_t>(capacity));

  // The empty pipe takes all but the last byte.
  const std::vector<char> data(capacity + 1, 'y');
  EXPECT_FALSE(WriteCompletely(write_end.get(), data.data(), data.size()));
}
#endif  // !NGLOG_OS_WINDOWS

#ifdef GTEST_HAS_DEATH_TEST
TEST(WriteToStderrOrExitDeathTest, WritesTheData) {
  constexpr char kData[] = "reaches stderr";
  EXPECT_EXIT(
      {
        nglog::WriteToStderrOrExit(kData, sizeof(kData) - 1);
        std::_Exit(EXIT_SUCCESS);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), kData);
}

TEST(WriteToStderrOrExitDeathTest, ExitsIfStderrIsClosed) {
  constexpr char kData[] = "lost";
  EXPECT_EXIT(
      {
        close(fileno(stderr));
        nglog::WriteToStderrOrExit(kData, sizeof(kData) - 1);
        std::_Exit(EXIT_SUCCESS);
      },
      testing::ExitedWithCode(nglog::kStderrWriteFailureExitCode), "");
}
#endif  // GTEST_HAS_DEATH_TEST

//! Frames of the synthetic stack dumps below.
constexpr int kDumpFrames = 3;

//! Returns `frames` frame lines as the stack dumps print them.
std::string FrameLines(int frames) {
  std::string lines;
  for (int i = 0; i < frames; ++i) {
    lines += "    @ 0x1234 Function()\n";
  }
  return lines;
}

TEST(HasCutOffStackDump, MatchesFramesFollowedByTheMarker) {
  EXPECT_THAT("*** Check failure stack trace: ***\n" + FrameLines(kDumpFrames) +
                  kStackDumpCutOffMarker,
              HasCutOffStackDump(kDumpFrames));
}

TEST(HasCutOffStackDump, MatchesLinesEndingWithCarriageReturns) {
  EXPECT_THAT("    @ 0x1234 Function()\r\n" +
                  nglog::TrimTrailingCRLF(kStackDumpCutOffMarker) + "\r\n",
              HasCutOffStackDump(1));
}

TEST(HasCutOffStackDump, ExplainsAnotherNumberOfFrames) {
  const std::string output =
      FrameLines(kDumpFrames + 1) + kStackDumpCutOffMarker;
  EXPECT_NONFATAL_FAILURE(EXPECT_THAT(output, HasCutOffStackDump(kDumpFrames)),
                          "which has 4 frames before the cut-off marker");
}

TEST(HasCutOffStackDump, ExplainsTheMarkerBeforeTheFrames) {
  const std::string output = kStackDumpCutOffMarker + FrameLines(kDumpFrames);
  EXPECT_NONFATAL_FAILURE(EXPECT_THAT(output, HasCutOffStackDump(kDumpFrames)),
                          "which has 0 frames before the cut-off marker");
}

TEST(HasCutOffStackDump, ExplainsAMissingMarker) {
  const std::string output = FrameLines(kDumpFrames);
  EXPECT_NONFATAL_FAILURE(EXPECT_THAT(output, HasCutOffStackDump(kDumpFrames)),
                          "which has no cut-off marker");
}

TEST(HasCutOffStackDump, CountsOnlyTheFramesRightBeforeTheMarker) {
  EXPECT_THAT(FrameLines(kDumpFrames) + "PC: @ 0x1234\n" + FrameLines(1) +
                  kStackDumpCutOffMarker,
              HasCutOffStackDump(1));
}

TEST(HasCutOffStackDump, DescribesTheExpectedDump) {
  EXPECT_EQ(testing::DescribeMatcher<std::string>(HasCutOffStackDump(2)),
            "has a stack dump of 2 frames that ends with the cut-off marker");
  EXPECT_EQ(testing::DescribeMatcher<std::string>(Not(HasCutOffStackDump(2))),
            "has no stack dump of 2 frames that ends with the cut-off marker");
}

}  // namespace
