// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause

#include "testing_utilities.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>

namespace {

using nglog::MatchesGoldenFile;
using nglog::ScopedStreamCapture;
using nglog::StandardStream;
using testing::AllOf;
using testing::HasSubstr;
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

std::string WriteGoldenFile(const std::string& name,
                            const std::string& contents) {
  const std::string path = nglog::TestTmpDir() + "/" + name;
  std::ofstream{path} << contents;
  return path;
}

TEST(MatchesGoldenFile, IgnoresRunSpecificDetails) {
  const std::string golden = WriteGoldenFile(
      "normalized.golden", "IYEARDATE TIME__ file.cc:LINE] message\n");
  EXPECT_TRUE(
      MatchesGoldenFile("I20260102 030405 file.cc:42] message\n", golden));
}

TEST(MatchesGoldenFile, ExplainsMismatchesWithADiff) {
  const std::string golden =
      WriteGoldenFile("mismatch.golden", "same\nexpected\n");
  const testing::AssertionResult result =
      MatchesGoldenFile("same\nactual\n", golden);
  EXPECT_FALSE(result);
  EXPECT_THAT(result.message(),
              AllOf(HasSubstr("- expected"), HasSubstr("+ actual"),
                    HasSubstr("  same"), Not(HasSubstr("- same"))));
}

TEST(MatchesGoldenFile, LimitsTheDiffToLinesAroundChanges) {
  const std::string golden = WriteGoldenFile(
      "context.golden", "distant\nnear 1\nnear 2\nnear 3\nexpected\n");
  const testing::AssertionResult result =
      MatchesGoldenFile("distant\nnear 1\nnear 2\nnear 3\nactual\n", golden);
  EXPECT_FALSE(result);
  EXPECT_THAT(result.message(),
              AllOf(HasSubstr("  near 1"), Not(HasSubstr("distant"))));
}

TEST(MatchesGoldenFile, ReportsAMissingGoldenFile) {
  const testing::AssertionResult result =
      MatchesGoldenFile("", nglog::TestTmpDir() + "/missing.golden");
  EXPECT_FALSE(result);
  EXPECT_THAT(result.message(), HasSubstr("missing.golden"));
}

}  // namespace
