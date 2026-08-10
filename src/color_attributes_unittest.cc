// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cerrno>
#include <cstdlib>
#include <sstream>
#include <string>

#include "internal/style_recorder.h"
#include "internal/theme.h"
#include "ng-log/internal/styled_value.h"
#include "ng-log/logging.h"
#include "testing_utilities.h"

using testing::HasSubstr;

#ifdef NGLOG_USE_GFLAGS
#  include <gflags/gflags.h>
using namespace GFLAGS_NAMESPACE;
#endif

using namespace nglog;
using namespace nglog::internal;

namespace {

using nglog::LogMessage;

TEST(ColorAttributes, DirectAppendPreservesPlainText) {
  std::string message;
  const TextAttributes attributes{
      ColorSpec{Color::kYellow, TextStyle::kNone, Color::kDefault},
      Hyperlink("file:///tmp/example.cc")};

  {
    LogMessage log("attributes.cc", 42, NGLOG_INFO, &message);
    log.AppendText("direct", 6, attributes);
  }

  EXPECT_EQ(message, "direct");
}

TEST(ColorAttributes, StyledValueWorksWithLogStream) {
  std::string message;

  {
    LogMessage log("attributes.cc", 42, NGLOG_INFO, &message);
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

TEST(ColorAttributes, NestedStylesRestoreThePreviousStyle) {
  nglog::internal::StyleRecorder recorder;
  const TextAttributes outer{
      ColorSpec{Color::kYellow, TextStyle::kNone, Color::kDefault},
      Hyperlink()};
  const TextAttributes inner{
      ColorSpec{Color::kCyan, TextStyle::kBold, Color::kDefault}, Hyperlink()};

  recorder.Push(0, outer);
  recorder.Push(2, inner);
  recorder.Pop(4);
  recorder.Pop(6);

  ASSERT_EQ(recorder.size(), 3U);
  EXPECT_EQ(recorder.span(0).begin, 0U);
  EXPECT_EQ(recorder.span(0).end, 2U);
  EXPECT_EQ(recorder.span(0).attributes.color, outer.color);
  EXPECT_EQ(recorder.span(1).begin, 2U);
  EXPECT_EQ(recorder.span(1).end, 4U);
  EXPECT_EQ(recorder.span(1).attributes.color, inner.color);
  EXPECT_EQ(recorder.span(2).begin, 4U);
  EXPECT_EQ(recorder.span(2).end, 6U);
  EXPECT_EQ(recorder.span(2).attributes.color, outer.color);
}

TEST(ColorAttributes, HyperlinkUriIsCopiedIntoTheRecordedSpan) {
  nglog::internal::StyleRecorder recorder;
  std::string uri = "file:///tmp/example.cc#part";
  recorder.Push(0, TextAttributes{ColorSpec{}, Hyperlink(uri.c_str())});
  uri = "changed";
  recorder.Pop(4);

  ASSERT_EQ(recorder.size(), 1U);
  EXPECT_STREQ(recorder.span(0).attributes.hyperlink.uri(),
               "file:///tmp/example.cc#part");
}

TEST(ColorAttributes, OversizedHyperlinkUriDisablesStyling) {
  nglog::internal::StyleRecorder recorder;
  const std::string uri(nglog::internal::StyleRecorder::kMaxUriBytes, 'u');

  recorder.Push(0, TextAttributes{ColorSpec{}, Hyperlink(uri.c_str())});
  recorder.Pop(2);

  EXPECT_EQ(recorder.size(), 0U);
}

TEST(ColorAttributes, NullHyperlinkUriIsStoredWithoutAUri) {
  nglog::internal::StyleRecorder recorder;
  recorder.Push(0, TextAttributes{ColorSpec{}, Hyperlink(nullptr)});
  recorder.Pop(1);

  ASSERT_EQ(recorder.size(), 1U);
  EXPECT_EQ(recorder.span(0).attributes.hyperlink.uri(), nullptr);
}

TEST(ColorAttributes, ClosingAnActiveStyleRecordsItsFinalSpan) {
  nglog::internal::StyleRecorder recorder;
  const TextAttributes attributes{ColorSpec{}, Hyperlink()};

  recorder.Pop(1);
  recorder.Push(1, attributes);
  recorder.Close(5);
  recorder.Close(6);

  ASSERT_EQ(recorder.size(), 1U);
  EXPECT_EQ(recorder.span(0).begin, 1U);
  EXPECT_EQ(recorder.span(0).end, 5U);
}

TEST(ColorAttributes, ActiveStyleLimitFallsBackToPlainText) {
  nglog::internal::StyleRecorder recorder;
  const TextAttributes attributes{ColorSpec{}, Hyperlink()};

  for (std::size_t index = 0;
       index <= nglog::internal::StyleRecorder::kMaxSpans; ++index) {
    recorder.Push(0, attributes);
  }

  EXPECT_EQ(recorder.size(), 0U);
}

TEST(ColorAttributes, FailedRecordingRemainsDisabled) {
  const TextAttributes attributes{ColorSpec{}, Hyperlink()};

  nglog::internal::StyleRecorder nested_recorder;
  for (std::size_t index = 0; index < nglog::internal::StyleRecorder::kMaxSpans;
       ++index) {
    nested_recorder.Push(index, attributes);
    nested_recorder.Pop(index + 1);
  }
  nested_recorder.Push(0, attributes);
  nested_recorder.Push(1, attributes);
  EXPECT_EQ(nested_recorder.size(), 0U);
  nested_recorder.Push(2, attributes);
  nested_recorder.Close(3);

  nglog::internal::StyleRecorder close_recorder;
  for (std::size_t index = 0; index < nglog::internal::StyleRecorder::kMaxSpans;
       ++index) {
    close_recorder.Push(index, attributes);
    close_recorder.Pop(index + 1);
  }
  close_recorder.Push(0, attributes);
  close_recorder.Close(1);
  EXPECT_EQ(close_recorder.size(), 0U);
}

TEST(ColorAttributes, ExcessiveStyleSpansFallBackToPlainText) {
  nglog::internal::StyleRecorder recorder;
  const TextAttributes attributes{ColorSpec{}, Hyperlink()};

  for (std::size_t index = 0;
       index <= nglog::internal::StyleRecorder::kMaxSpans; ++index) {
    recorder.Push(index, attributes);
    recorder.Pop(index + 1);
  }

  EXPECT_EQ(recorder.size(), 0U);
}

TEST(ColorAttributes, TruncatedStyledTextRemainsBounded) {
  const bool old_log_prefix = FLAGS_log_prefix;
  FLAGS_log_prefix = false;
  std::string message;
  const std::string text(LogMessage::kMaxLogMessageLen + 1, 'x');

  {
    LogMessage log("attributes.cc", 42, NGLOG_INFO, &message);
    log.AppendText(text.data(), text.size(), TextAttributes{});
  }

  FLAGS_log_prefix = old_log_prefix;
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
TEST(ColorAttributes, DefaultPrefixRetainsPerFieldColors) {
  ASSERT_EQ(setenv("CLICOLOR_FORCE", "1", 1), 0);
  ASSERT_EQ(setenv("TERM", "xterm", 1), 0);
  unsetenv("NO_COLOR");

  const bool old_logtostderr = FLAGS_logtostderr;
  const bool old_colorlogtostderr = FLAGS_colorlogtostderr;
  const bool old_log_prefix = FLAGS_log_prefix;
  const int old_minloglevel = FLAGS_minloglevel;
  const bool old_symbolize_hyperlinks = FLAGS_symbolize_hyperlinks;
  FLAGS_logtostderr = true;
  FLAGS_colorlogtostderr = true;
  FLAGS_log_prefix = true;
  FLAGS_minloglevel = NGLOG_INFO;
  FLAGS_symbolize_hyperlinks = false;

  CaptureTestStderr();
  LOG(INFO) << "colored default prefix";
  const std::string output = GetCapturedTestStderr();

  FLAGS_logtostderr = old_logtostderr;
  FLAGS_colorlogtostderr = old_colorlogtostderr;
  FLAGS_log_prefix = old_log_prefix;
  FLAGS_minloglevel = old_minloglevel;
  FLAGS_symbolize_hyperlinks = old_symbolize_hyperlinks;

  EXPECT_THAT(output, HasSubstr("\033[34m"));
  EXPECT_THAT(output, HasSubstr("\033[35m"));
  EXPECT_THAT(output, HasSubstr("\033[36m"));
  EXPECT_THAT(output, HasSubstr("\033[90m]"));
}

TEST(ColorAttributes, RendersStyledSpansAndHyperlinksOnAnsiOutput) {
  ASSERT_EQ(setenv("CLICOLOR_FORCE", "1", 1), 0);
  ASSERT_EQ(setenv("TERM", "xterm", 1), 0);
  ASSERT_EQ(setenv("TERM_PROGRAM", "iTerm.app", 1), 0);
  unsetenv("NO_COLOR");

  const bool old_logtostderr = FLAGS_logtostderr;
  const bool old_colorlogtostderr = FLAGS_colorlogtostderr;
  const bool old_log_prefix = FLAGS_log_prefix;
  const bool old_symbolize_hyperlinks = FLAGS_symbolize_hyperlinks;
  FLAGS_logtostderr = true;
  FLAGS_colorlogtostderr = true;
  FLAGS_log_prefix = false;
  FLAGS_symbolize_hyperlinks = true;

  CaptureTestStderr();
  {
    LogMessage log("attributes.cc", 42, NGLOG_INFO);
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
  const std::string output = GetCapturedTestStderr();

  FLAGS_logtostderr = old_logtostderr;
  FLAGS_colorlogtostderr = old_colorlogtostderr;
  FLAGS_log_prefix = old_log_prefix;
  FLAGS_symbolize_hyperlinks = old_symbolize_hyperlinks;

  EXPECT_THAT(
      output,
      HasSubstr("\033]8;;file:///tmp/example.cc\033\\\033[1;36mlinked"));
  EXPECT_THAT(output, HasSubstr("before"));
  EXPECT_THAT(output, HasSubstr("after"));
  EXPECT_THAT(output, HasSubstr("\033[2;39m: "));
  EXPECT_THAT(output, HasSubstr("\033[1;36m [2]\033[0m"));
}

TEST(ColorAttributes, ResetsAttributesBeforeTheLineEnding) {
  ASSERT_EQ(setenv("CLICOLOR_FORCE", "1", 1), 0);
  ASSERT_EQ(setenv("TERM", "xterm", 1), 0);
  unsetenv("NO_COLOR");

  const bool old_logtostderr = FLAGS_logtostderr;
  const bool old_colorlogtostderr = FLAGS_colorlogtostderr;
  const bool old_log_prefix = FLAGS_log_prefix;
  FLAGS_logtostderr = true;
  FLAGS_colorlogtostderr = true;
  FLAGS_log_prefix = false;

  CaptureTestStderr();
  LOG(INFO) << "colored line";
  const std::string output = GetCapturedTestStderr();

  FLAGS_logtostderr = old_logtostderr;
  FLAGS_colorlogtostderr = old_colorlogtostderr;
  FLAGS_log_prefix = old_log_prefix;

  EXPECT_THAT(output, HasSubstr("colored line\033[0m\n"));
}

TEST(ColorAttributes, ResetsAndReappliesAttributesForEachLine) {
  ASSERT_EQ(setenv("CLICOLOR_FORCE", "1", 1), 0);
  ASSERT_EQ(setenv("TERM", "xterm", 1), 0);
  unsetenv("NO_COLOR");

  const bool old_logtostderr = FLAGS_logtostderr;
  const bool old_colorlogtostderr = FLAGS_colorlogtostderr;
  const bool old_log_prefix = FLAGS_log_prefix;
  FLAGS_logtostderr = true;
  FLAGS_colorlogtostderr = true;
  FLAGS_log_prefix = false;

  CaptureTestStderr();
  LOG(INFO) << Styled(
      ColorSpec{Color::kCyan, TextStyle::kBold, Color::kDefault},
      "first\nsecond");
  const std::string output = GetCapturedTestStderr();

  FLAGS_logtostderr = old_logtostderr;
  FLAGS_colorlogtostderr = old_colorlogtostderr;
  FLAGS_log_prefix = old_log_prefix;

  EXPECT_THAT(output, HasSubstr("first\033[0m\n\033[1;36msecond\033[0m\n"));
}
#endif

}  // namespace

int main(int argc, char** argv) {
  InitializeLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);
#ifdef NGLOG_USE_GFLAGS
  ParseCommandLineFlags(&argc, &argv, true);
#endif
  return RUN_ALL_TESTS();
}
