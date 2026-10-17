// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include "internal/style_recorder.h"
#include "internal/theme.h"
#include "ng-log/internal/styled_value.h"
#include "ng-log/logging.h"
#include "testing_utilities.h"

using testing::AllOf;
using testing::ElementsAre;
using testing::Field;
using testing::HasSubstr;
using testing::IsEmpty;
using testing::IsNull;

using namespace nglog;
using namespace nglog::internal;

namespace {

constexpr char kSourceFile[] = "attributes.cc";
constexpr int kSourceLine = 42;

TEST(ColorAttributes, DirectAppendPreservesPlainText) {
  std::string message;
  const std::string text = "direct";
  const TextAttributes attributes{
      ColorSpec{Color::kYellow, TextStyle::kNone, Color::kDefault},
      Hyperlink("file:///tmp/example.cc")};

  {
    LogMessage log(kSourceFile, kSourceLine, NGLOG_INFO, &message);
    log.AppendText(text.data(), text.size(), attributes);
  }

  EXPECT_EQ(message, text);
}

TEST(ColorAttributes, StyledValueWorksWithLogStream) {
  std::string message;

  {
    LogMessage log(kSourceFile, kSourceLine, NGLOG_INFO, &message);
    log.stream() << Styled(
        ColorSpec{Color::kCyan, TextStyle::kNone, Color::kDefault},
        std::string("value"));
  }

  EXPECT_EQ(message, "value");
}

TEST(ColorAttributes, StyledValueWorksWithOrdinaryStream) {
  std::ostringstream output;
  output << Styled(ColorSpec{Color::kGreen, TextStyle::kBold, Color::kDefault},
                   "value");

  EXPECT_EQ(output.str(), "value");
}

//! Copies the spans of the recorder into a container for matching.
std::vector<StyleRecorder::Span> RecordedSpans(const StyleRecorder& recorder) {
  std::vector<StyleRecorder::Span> spans;
  for (std::size_t index = 0; index < recorder.size(); ++index) {
    spans.push_back(recorder.span(index));
  }
  return spans;
}

MATCHER_P2(SpanCovers, begin, end,
           "covers [" + testing::PrintToString(begin) + ", " +
               testing::PrintToString(end) + ")") {
  *result_listener << "covers [" << arg.begin << ", " << arg.end << ")";
  return arg.begin == begin && arg.end == end;
}

//! Matches a recorded span covering [begin, end) in the given color.
auto ColoredSpanCovers(std::size_t begin, std::size_t end,
                       const ColorSpec& color) {
  return AllOf(SpanCovers(begin, end),
               Field(&StyleRecorder::Span::attributes,
                     Field(&TextAttributes::color, color)));
}

//! Records the given number of adjacent single-character spans.
void RecordAdjacentSpans(StyleRecorder& recorder, std::size_t count,
                         const TextAttributes& attributes) {
  for (std::size_t index = 0; index < count; ++index) {
    recorder.Push(index, attributes);
    recorder.Pop(index + 1);
  }
}

TEST(ColorAttributes, NestedStylesRestoreThePreviousStyle) {
  StyleRecorder recorder;
  const TextAttributes outer{
      ColorSpec{Color::kYellow, TextStyle::kNone, Color::kDefault},
      Hyperlink()};
  const TextAttributes inner{
      ColorSpec{Color::kCyan, TextStyle::kBold, Color::kDefault}, Hyperlink()};

  recorder.Push(0, outer);
  recorder.Push(2, inner);
  recorder.Pop(4);
  recorder.Pop(6);

  EXPECT_THAT(RecordedSpans(recorder),
              ElementsAre(ColoredSpanCovers(0U, 2U, outer.color),
                          ColoredSpanCovers(2U, 4U, inner.color),
                          ColoredSpanCovers(4U, 6U, outer.color)));
}

TEST(ColorAttributes, HyperlinkUriIsCopiedIntoTheRecordedSpan) {
  StyleRecorder recorder;
  std::string uri = "file:///tmp/example.cc#part";
  recorder.Push(0, TextAttributes{ColorSpec{}, Hyperlink(uri.c_str())});
  uri = "changed";
  recorder.Pop(4);

  ASSERT_EQ(recorder.size(), 1U);
  EXPECT_STREQ(recorder.span(0).attributes.hyperlink.uri(),
               "file:///tmp/example.cc#part");
}

TEST(ColorAttributes, OversizedHyperlinkUriDisablesStyling) {
  StyleRecorder recorder;
  const std::string uri(StyleRecorder::kMaxUriBytes, 'u');

  recorder.Push(0, TextAttributes{ColorSpec{}, Hyperlink(uri.c_str())});
  recorder.Pop(2);

  EXPECT_THAT(RecordedSpans(recorder), IsEmpty());
}

TEST(ColorAttributes, NullHyperlinkUriIsStoredWithoutAUri) {
  StyleRecorder recorder;
  recorder.Push(0, TextAttributes{ColorSpec{}, Hyperlink(nullptr)});
  recorder.Pop(1);

  ASSERT_EQ(recorder.size(), 1U);
  EXPECT_THAT(recorder.span(0).attributes.hyperlink.uri(), IsNull());
}

TEST(ColorAttributes, ClosingAnActiveStyleRecordsItsFinalSpan) {
  StyleRecorder recorder;
  const TextAttributes attributes{ColorSpec{}, Hyperlink()};

  recorder.Pop(1);
  recorder.Push(1, attributes);
  recorder.Close(5);
  recorder.Close(6);

  EXPECT_THAT(RecordedSpans(recorder), ElementsAre(SpanCovers(1U, 5U)));
}

TEST(ColorAttributes, ActiveStyleLimitFallsBackToPlainText) {
  StyleRecorder recorder;
  const TextAttributes attributes{ColorSpec{}, Hyperlink()};

  for (std::size_t index = 0; index <= StyleRecorder::kMaxSpans; ++index) {
    recorder.Push(0, attributes);
  }

  EXPECT_THAT(RecordedSpans(recorder), IsEmpty());
}

TEST(ColorAttributes, FailedNestedRecordingRemainsDisabled) {
  const TextAttributes attributes{ColorSpec{}, Hyperlink()};
  StyleRecorder recorder;
  RecordAdjacentSpans(recorder, StyleRecorder::kMaxSpans, attributes);

  recorder.Push(0, attributes);
  recorder.Push(1, attributes);
  EXPECT_THAT(RecordedSpans(recorder), IsEmpty());
  recorder.Push(2, attributes);
  recorder.Close(3);
  EXPECT_THAT(RecordedSpans(recorder), IsEmpty());
}

TEST(ColorAttributes, FailedClosingRecordingRemainsDisabled) {
  const TextAttributes attributes{ColorSpec{}, Hyperlink()};
  StyleRecorder recorder;
  RecordAdjacentSpans(recorder, StyleRecorder::kMaxSpans, attributes);

  recorder.Push(0, attributes);
  recorder.Close(1);
  EXPECT_THAT(RecordedSpans(recorder), IsEmpty());
}

TEST(ColorAttributes, ExcessiveStyleSpansFallBackToPlainText) {
  StyleRecorder recorder;
  RecordAdjacentSpans(recorder, StyleRecorder::kMaxSpans + 1,
                      TextAttributes{ColorSpec{}, Hyperlink()});

  EXPECT_THAT(RecordedSpans(recorder), IsEmpty());
}

TEST(ColorAttributes, TruncatedStyledTextRemainsBounded) {
  std::string message;
  const std::string text(LogMessage::kMaxLogMessageLen + 1, 'x');

  {
    const auto flags =
        nglog::MakeScopedFlags(nglog::MakeScopedFlag(FLAGS_log_prefix, false));
    LogMessage log(kSourceFile, kSourceLine, NGLOG_INFO, &message);
    log.AppendText(text.data(), text.size(), TextAttributes{});
  }

  EXPECT_EQ(message.size(), LogMessage::kMaxLogMessageLen - 2);
  EXPECT_EQ(message.back(), 'x');
}

TEST(ColorAttributes, ErrnoThemeUsesDistinctRoles) {
  const ColorSpec message = DefaultTheme().Get(Role::kErrnoMessage);
  const ColorSpec code = DefaultTheme().Get(Role::kErrnoCode);

  EXPECT_NE(message, code);
  EXPECT_EQ(message.style, TextStyle::kDim);
  EXPECT_EQ(code.style, TextStyle::kBold);
}

#if !defined(NGLOG_OS_WINDOWS)
//! Forces colored log output to stderr and restores flags and environment.
class ColoredStderr : public testing::Test {
 protected:
  void SetUp() override {
    nglog::SetFlag(FLAGS_logtostderr, true);
    nglog::SetFlag(FLAGS_colorlogtostderr, true);
    nglog::SetFlag(FLAGS_log_prefix, false);
  }

 private:
  nglog::ScopedFlags<> saved_flags_;
  nglog::ScopedEnvironmentVariable clicolor_force_{"CLICOLOR_FORCE", "1"};
  nglog::ScopedEnvironmentVariable term_{"TERM", "xterm"};
  nglog::ScopedEnvironmentVariable no_color_{"NO_COLOR", nullptr};
};

TEST_F(ColoredStderr, DefaultPrefixRetainsPerFieldColors) {
  nglog::SetFlag(FLAGS_log_prefix, true);
  nglog::SetFlag(FLAGS_minloglevel, static_cast<std::int32_t>(NGLOG_INFO));
  nglog::SetFlag(FLAGS_symbolize_hyperlinks, false);

  ScopedStreamCapture capture{StandardStream::kStderr};
  LOG(INFO) << "colored default prefix";
  const std::string output = capture.Stop();

  EXPECT_THAT(output, AllOf(HasSubstr("\033[34m"), HasSubstr("\033[35m"),
                            HasSubstr("\033[36m"), HasSubstr("\033[90m]")));
}

TEST_F(ColoredStderr, RendersStyledSpansAndHyperlinksOnAnsiOutput) {
  const nglog::ScopedEnvironmentVariable term_program{"TERM_PROGRAM",
                                                      "iTerm.app"};
  nglog::SetFlag(FLAGS_symbolize_hyperlinks, true);

  ScopedStreamCapture capture{StandardStream::kStderr};
  {
    LogMessage log(kSourceFile, kSourceLine, NGLOG_INFO);
    log.stream() << "before"
                 << Styled(
                        TextAttributes{ColorSpec{Color::kCyan, TextStyle::kBold,
                                                 Color::kDefault},
                                       Hyperlink("file:///tmp/example.cc")},
                        "linked")
                 << "after";
  }
  errno = ENOENT;
  PLOG(INFO) << "plog";
  const std::string output = capture.Stop();

  EXPECT_THAT(
      output,
      AllOf(HasSubstr("\033]8;;file:///tmp/example.cc\033\\\033[1;36mlinked"),
            HasSubstr("before"), HasSubstr("after"), HasSubstr("\033[2;39m: "),
            HasSubstr("\033[1;36m [" + std::to_string(ENOENT) + "]\033[0m")));
}

TEST_F(ColoredStderr, ResetsAttributesBeforeTheLineEnding) {
  ScopedStreamCapture capture{StandardStream::kStderr};
  LOG(INFO) << "colored line";
  const std::string output = capture.Stop();

  EXPECT_THAT(output, HasSubstr("colored line\033[0m\n"));
}

TEST_F(ColoredStderr, ResetsAndReappliesAttributesForEachLine) {
  ScopedStreamCapture capture{StandardStream::kStderr};
  LOG(INFO) << Styled(
      ColorSpec{Color::kCyan, TextStyle::kBold, Color::kDefault},
      "first\nsecond");
  const std::string output = capture.Stop();

  EXPECT_THAT(output, HasSubstr("first\033[0m\n\033[1;36msecond\033[0m\n"));
}
#endif

}  // namespace

int main(int argc, char** argv) {
  InitializeTestLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);
  nglog::ParseCommandLineFlags(&argc, &argv,
                               nglog::FlagParsingMode::kRemoveFlags);
  return RUN_ALL_TESTS();
}
