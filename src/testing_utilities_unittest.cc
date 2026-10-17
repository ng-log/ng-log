// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause

#include "testing_utilities.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

namespace {

using nglog::ScopedStreamCapture;
using nglog::StandardStream;

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

}  // namespace
