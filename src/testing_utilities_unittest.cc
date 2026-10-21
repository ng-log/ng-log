// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause

#include "testing_utilities.h"

#include <gmock/gmock.h>
#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "stacktrace.h"

namespace {

using nglog::HasCutOffStackDump;
using nglog::kStackDumpCutOffMarker;
using nglog::ScopedStreamCapture;
using nglog::StandardStream;
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
  ScopedStreamCapture capture{StandardStream::kStderr};
  std::fputs("once", stderr);
  const std::string output = capture.Stop();
  EXPECT_EQ(capture.Stop(), output);
}

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
