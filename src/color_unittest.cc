// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "internal/source_location.h"
#include "internal/styled_output.h"
#include "internal/terminal_capabilities.h"
#include "internal/theme.h"
#include "ng-log/internal/color_spec.h"
#include "ng-log/internal/hyperlink.h"
#include "ng-log/logging.h"
#include "testing_utilities.h"

#ifdef NGLOG_OS_WINDOWS
#  include <windows.h>
#endif

#ifdef NGLOG_OS_EMSCRIPTEN
#  include <emscripten.h>
#endif

#ifdef NGLOG_USE_GFLAGS
#  include <gflags/gflags.h>
using namespace GFLAGS_NAMESPACE;
#endif

using namespace nglog;
using namespace nglog::internal;
using testing::HasSubstr;
using testing::IsEmpty;
using testing::IsNull;
using testing::Not;
#ifdef NGLOG_OS_WINDOWS
using testing::ElementsAre;
using testing::StrEq;
#endif

namespace {

TEST(Flags, SymbolizeFileBasePathIgnoresEnvironment) {
  EXPECT_THAT(FLAGS_symbolize_file_base_path, IsEmpty());
}

TEST(ShouldColorize, NoColorEnvWins) {
  EXPECT_FALSE(ShouldColorize(/*is_a_tty=*/true, "xterm", /*no_color_env=*/"",
                              /*clicolor_force_env=*/nullptr));
  EXPECT_FALSE(ShouldColorize(/*is_a_tty=*/true, "xterm",
                              /*no_color_env=*/"1", "1"));
}

TEST(ShouldColorize, CliColorForceOverridesNonTty) {
  EXPECT_TRUE(ShouldColorize(/*is_a_tty=*/false, "xterm",
                             /*no_color_env=*/nullptr,
                             /*clicolor_force_env=*/"1"));
  EXPECT_FALSE(ShouldColorize(/*is_a_tty=*/false, "xterm",
                              /*no_color_env=*/nullptr,
                              /*clicolor_force_env=*/"0"));
  EXPECT_FALSE(ShouldColorize(/*is_a_tty=*/false, "xterm",
                              /*no_color_env=*/nullptr,
                              /*clicolor_force_env=*/nullptr));
}

TEST(ShouldColorize, RequiresTty) {
  EXPECT_FALSE(ShouldColorize(/*is_a_tty=*/false, "xterm-256color",
                              /*no_color_env=*/nullptr,
                              /*clicolor_force_env=*/nullptr));
  EXPECT_TRUE(ShouldColorize(/*is_a_tty=*/true, "xterm-256color",
                             /*no_color_env=*/nullptr,
                             /*clicolor_force_env=*/nullptr));
}

TEST(ShouldColorize, RejectsDumbTerm) {
  EXPECT_FALSE(ShouldColorize(/*is_a_tty=*/true, "dumb",
                              /*no_color_env=*/nullptr,
                              /*clicolor_force_env=*/nullptr));
}

TEST(ShouldColorize, AcceptsUnknownTerm) {
  // Modern terminals are almost universally ANSI-capable. Only "dumb" is
  // specifically rejected, rather than requiring an allow-list match.
  EXPECT_TRUE(ShouldColorize(/*is_a_tty=*/true, "some-new-terminal",
                             /*no_color_env=*/nullptr,
                             /*clicolor_force_env=*/nullptr));
  EXPECT_TRUE(ShouldColorize(/*is_a_tty=*/true, /*term=*/nullptr,
                             /*no_color_env=*/nullptr,
                             /*clicolor_force_env=*/nullptr));
}

TEST(ShouldEnableHyperlinks, RequiresColorize) {
  EXPECT_FALSE(ShouldEnableHyperlinks(/*colorize=*/false, "1", nullptr, nullptr,
                                      nullptr));
}

TEST(ShouldEnableHyperlinks, DetectsKnownTerminals) {
  EXPECT_TRUE(ShouldEnableHyperlinks(/*colorize=*/true, "1", nullptr, nullptr,
                                     nullptr));
  EXPECT_TRUE(ShouldEnableHyperlinks(/*colorize=*/true, nullptr, nullptr, "6.4",
                                     nullptr));
  EXPECT_TRUE(ShouldEnableHyperlinks(/*colorize=*/true, nullptr, nullptr,
                                     nullptr, "22.08"));
  EXPECT_TRUE(ShouldEnableHyperlinks(/*colorize=*/true, nullptr, "iTerm.app",
                                     nullptr, nullptr));
}

TEST(ShouldEnableHyperlinks, RejectsUnknownTerminal) {
  EXPECT_FALSE(ShouldEnableHyperlinks(/*colorize=*/true, nullptr, "SomeTerm",
                                      nullptr, nullptr));
  EXPECT_FALSE(ShouldEnableHyperlinks(/*colorize=*/true, nullptr, nullptr,
                                      nullptr, nullptr));
}

TEST(FormatAnsiSequence, IncludesForegroundAndStyle) {
  char buf[32];
  ColorSpec{Color::kRed, TextStyle::kNone, Color::kDefault}.FormatAnsiSequence(
      buf, sizeof(buf));
  EXPECT_STREQ("\033[31m", buf);

  ColorSpec{Color::kGray, TextStyle::kNone, Color::kDefault}.FormatAnsiSequence(
      buf, sizeof(buf));
  EXPECT_STREQ("\033[90m", buf);

  ColorSpec{Color::kGreen, TextStyle::kBold, Color::kDefault}
      .FormatAnsiSequence(buf, sizeof(buf));
  EXPECT_STREQ("\033[1;32m", buf);
}

TEST(FormatAnsiSequence, IncludesBackground) {
  char buf[32];
  ColorSpec{Color::kWhite, TextStyle::kNone, Color::kBlue}.FormatAnsiSequence(
      buf, sizeof(buf));
  EXPECT_STREQ("\033[37;44m", buf);
}

TEST(FormatAnsiSequence, SupportsRgb) {
  char buf[32];
  ColorSpec{RgbColor{255, 128, 0}, TextStyle::kNone, Color::kDefault}
      .FormatAnsiSequence(buf, sizeof(buf));
  EXPECT_STREQ("\033[38;2;255;128;0m", buf);
}

TEST(FormatAnsiSequence, NeverOverflowsAndAlwaysTerminates) {
  char buf[6];
  ColorSpec{Color::kGreen, TextStyle::kBold, Color::kDefault}
      .FormatAnsiSequence(buf, sizeof(buf));
  // Whatever got written must be NUL-terminated within bounds.
  EXPECT_THAT(std::memchr(buf, '\0', sizeof(buf)), Not(IsNull()));
}

TEST(FormatAnsiSequence, HandlesSmallBuffersAndAllTextStyles) {
  char buf[1] = {'x'};
  ColorSpec{Color::kDefault, TextStyle::kNone, Color::kDefault}
      .FormatAnsiSequence(buf, 0);
  EXPECT_EQ(buf[0], 'x');

  ColorSpec{Color::kDefault, TextStyle::kNone, Color::kDefault}
      .FormatAnsiSequence(buf, sizeof(buf));
  EXPECT_EQ(buf[0], '\0');

  char sequence[32];
  ColorSpec{Color::kDefault,
            TextStyle::kDim | TextStyle::kItalic | TextStyle::kUnderline,
            Color::kDefault}
      .FormatAnsiSequence(sequence, sizeof(sequence));
  EXPECT_STREQ("\033[2;3;4;39m", sequence);
}

TEST(TextStyle, OperatorsUseTheEnumUnderlyingType) {
  using UnderlyingType = std::underlying_type_t<TextStyle>;
  const TextStyle style = TextStyle::kBold | TextStyle::kUnderline;
  EXPECT_EQ(static_cast<UnderlyingType>(style),
            static_cast<UnderlyingType>(TextStyle::kBold) |
                static_cast<UnderlyingType>(TextStyle::kUnderline));
}

#ifdef NGLOG_OS_WINDOWS
TEST(LegacyConsole, MapsStylesAndColorsToConsoleAttributes) {
  EXPECT_EQ(LegacyConsoleAttribute(
                ColorSpec{Color::kRed, TextStyle::kNone, Color::kDefault}),
            FOREGROUND_RED);
  EXPECT_EQ(LegacyConsoleAttribute(
                ColorSpec{Color::kBrightCyan, TextStyle::kBold, Color::kRed}),
            FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY |
                BACKGROUND_RED);
  EXPECT_EQ(LegacyConsoleAttribute(ColorSpec{
                RgbColor{128, 0, 255}, TextStyle::kNone, Color::kDefault}),
            FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
  EXPECT_EQ(LegacyConsoleAttribute(ColorSpec{Color::kDefault, TextStyle::kNone,
                                             Color::kBrightWhite}),
            FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE |
                BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE |
                BACKGROUND_INTENSITY);
}

TEST(LegacyConsole, AppliesAndRestoresAttributesAroundWrittenText) {
  const HANDLE console = reinterpret_cast<HANDLE>(1);
  CONSOLE_SCREEN_BUFFER_INFO info = {};
  info.wAttributes = FOREGROUND_RED | BACKGROUND_BLUE;
  const ColorSpec spec{Color::kBrightCyan, TextStyle::kBold, Color::kRed};
  std::vector<WORD> applied_attributes;
  std::vector<std::string> events;
  std::string written_text;

  const auto get_info = [console, &info](
                            HANDLE handle,
                            PCONSOLE_SCREEN_BUFFER_INFO result) -> BOOL {
    EXPECT_EQ(handle, console);
    *result = info;
    return TRUE;
  };
  const auto set_attributes = [console, &applied_attributes, &events](
                                  HANDLE handle, WORD attributes) {
    EXPECT_EQ(handle, console);
    events.emplace_back("set");
    applied_attributes.push_back(attributes);
    return TRUE;
  };
  const auto write_text = [&events, &written_text] {
    events.emplace_back("write");
    written_text.append("styled");
  };

  WithLegacyConsoleAttribute(console, spec, get_info, set_attributes,
                             write_text);

  EXPECT_THAT(applied_attributes,
              ElementsAre(LegacyConsoleAttribute(spec), info.wAttributes));
  EXPECT_THAT(events, ElementsAre("set", "write", "set"));
  EXPECT_THAT(written_text, StrEq("styled"));
}
#endif

TEST(Theme, DefaultThemeLeavesInfoUncolored) {
  const ColorSpec spec = DefaultTheme().Get(Role::kLogInfo);
  EXPECT_EQ(spec.foreground, Color::kDefault);
}

TEST(Theme, DefaultThemeAssignsDistinctStackRoles) {
  const Theme& theme = DefaultTheme();
  const ColorSpec address = theme.Get(Role::kStackAddress);
  const ColorSpec file = theme.Get(Role::kStackFile);
  const ColorSpec function = theme.Get(Role::kStackFunction);
  EXPECT_NE(address.foreground, file.foreground);
  EXPECT_NE(file.foreground, function.foreground);
  EXPECT_NE(address.foreground, function.foreground);
}

TEST(Theme, DefaultThemeUsesDistinctThreadNameColor) {
  const Theme& theme = DefaultTheme();
  EXPECT_EQ(theme.Get(Role::kMetaThreadName).foreground, Color::kCyan);
  EXPECT_NE(theme.Get(Role::kMetaThreadName).foreground,
            theme.Get(Role::kMetaIdentifier).foreground);
}

TEST(Theme, SetAndGetRoundTrip) {
  Theme theme;
  const ColorSpec spec{Color::kBrightMagenta, TextStyle::kUnderline,
                       Color::kDefault};
  theme.Set(Role::kStackFunction, spec);
  EXPECT_EQ(theme.Get(Role::kStackFunction).foreground, spec.foreground);
}

TEST(BuildFileLineUri, AcceptsPosixAbsolutePath) {
  char uri[256];
  const std::string span = "/src/foo.cc:42";
  EXPECT_TRUE(BuildFileLineUri(span.c_str(), span.size(),
                               /*base_path=*/nullptr, /*host=*/nullptr, uri,
                               sizeof(uri)));
  EXPECT_STREQ("file://localhost/src/foo.cc", uri);
}

TEST(BuildFileUriWithoutLine, AcceptsPosixAbsolutePath) {
  char uri[256];
  const std::string path = "/src/foo.cc";
  EXPECT_TRUE(BuildFileUri(path.c_str(), path.size(), /*base_path=*/nullptr,
                           /*host=*/nullptr, uri, sizeof(uri)));
  EXPECT_STREQ("file://localhost/src/foo.cc", uri);
}

TEST(BuildFileLineUri, AcceptsWindowsAbsolutePath) {
  char uri[256];
  const std::string span = "C:\\src\\foo.cc:42";
  EXPECT_TRUE(BuildFileLineUri(span.c_str(), span.size(),
                               /*base_path=*/nullptr, /*host=*/nullptr, uri,
                               sizeof(uri)));
  EXPECT_STREQ("file://localhost/C:/src/foo.cc", uri);
}

TEST(BuildFileLineUri, EncodesPathCharactersReservedByUris) {
  char uri[256];
  const std::string span = "/src/a file#part?.cc:42";
  EXPECT_TRUE(BuildFileLineUri(span.c_str(), span.size(),
                               /*base_path=*/nullptr, /*host=*/nullptr, uri,
                               sizeof(uri)));
  EXPECT_STREQ("file://localhost/src/a%20file%23part%3F.cc", uri);
}

TEST(BuildFileLineUri, EncodesWindowsPathSeparators) {
  char uri[256];
  const std::string span = "C:\\src\\a file.cc:42";
  EXPECT_TRUE(BuildFileLineUri(span.c_str(), span.size(),
                               /*base_path=*/nullptr, /*host=*/nullptr, uri,
                               sizeof(uri)));
  EXPECT_STREQ("file://localhost/C:/src/a%20file.cc", uri);
}

TEST(BuildFileLineUri, NormalizesWindowsBasePathSeparators) {
  char uri[256];
  const std::string span = "src\\foo.cc:42";
  EXPECT_TRUE(BuildFileLineUri(span.c_str(), span.size(), "C:\\build",
                               /*host=*/nullptr, uri, sizeof(uri)));
  EXPECT_STREQ("file://localhost/C:/build/src/foo.cc", uri);
}

TEST(BuildFileLineUri, UsesSuppliedHost) {
  char uri[256];
  const std::string span = "/src/foo.cc:42";
  EXPECT_TRUE(BuildFileLineUri(span.c_str(), span.size(),
                               /*base_path=*/nullptr, "my-host", uri,
                               sizeof(uri)));
  EXPECT_STREQ("file://my-host/src/foo.cc", uri);
}

TEST(BuildFileLineUri, ResolvesRelativePathAgainstBasePath) {
  char uri[256];
  const std::string span = "src/foo.cc:42";
  EXPECT_TRUE(BuildFileLineUri(span.c_str(), span.size(), "/build",
                               /*host=*/nullptr, uri, sizeof(uri)));
  EXPECT_STREQ("file://localhost/build/src/foo.cc", uri);
}

TEST(BuildFileLineUri, DoesNotDuplicateSeparatorWhenJoiningBasePath) {
  char uri[256];
  const std::string span = "src/foo.cc:42";
  EXPECT_TRUE(BuildFileLineUri(span.c_str(), span.size(), "/build/",
                               /*host=*/nullptr, uri, sizeof(uri)));
  EXPECT_STREQ("file://localhost/build/src/foo.cc", uri);
}

TEST(BuildFileLineUri, IgnoresBasePathWhenSpanIsAlreadyAbsolute) {
  char uri[256];
  const std::string span = "/src/foo.cc:42";
  EXPECT_TRUE(BuildFileLineUri(span.c_str(), span.size(), "/build",
                               /*host=*/nullptr, uri, sizeof(uri)));
  EXPECT_STREQ("file://localhost/src/foo.cc", uri);
}

TEST(BuildFileLineUri, RejectsRelativePathWithoutBasePath) {
  char uri[256];
  const std::string span = "src/foo.cc:42";
  EXPECT_FALSE(BuildFileLineUri(span.c_str(), span.size(),
                                /*base_path=*/nullptr, /*host=*/nullptr, uri,
                                sizeof(uri)));
  EXPECT_FALSE(BuildFileLineUri(span.c_str(), span.size(), /*base_path=*/"",
                                /*host=*/nullptr, uri, sizeof(uri)));
}

TEST(BuildFileLineUri, RejectsRelativeBasePath) {
  char uri[256];
  const std::string span = "src/foo.cc:42";
  EXPECT_FALSE(BuildFileLineUri(span.c_str(), span.size(), "relative/build",
                                /*host=*/nullptr, uri, sizeof(uri)));
}

TEST(BuildFileLineUri, RejectsMissingLineNumber) {
  char uri[256];
  const std::string span = "/src/foo.cc";
  EXPECT_FALSE(BuildFileLineUri(span.c_str(), span.size(),
                                /*base_path=*/nullptr, /*host=*/nullptr, uri,
                                sizeof(uri)));
}

TEST(BuildFileLineUri, RejectsTooSmallBuffer) {
  char uri[4];
  const std::string span = "/src/foo.cc:42";
  EXPECT_FALSE(BuildFileLineUri(span.c_str(), span.size(),
                                /*base_path=*/nullptr, /*host=*/nullptr, uri,
                                sizeof(uri)));
}

TEST(BuildFileLineUri, HandlesRepeatedSeparatorsInAPath) {
  char uri[256];
  const std::string span = "/src//nested/foo.cc:42";
  EXPECT_TRUE(BuildFileLineUri(span.c_str(), span.size(),
                               /*base_path=*/nullptr, /*host=*/nullptr, uri,
                               sizeof(uri)));
  EXPECT_STREQ("file://localhost/src//nested/foo.cc", uri);
}

// The libbacktrace/addr2line symbolize backends keep a decorative ':'
// after the line number as part of the "file:line" span passed here, so
// it renders in the same color as the rest of the span (see
// DumpStackFrameInfo() in signalhandler.cc). The line number must not
// leak into the URI just because of that trailing ':', or the link opens
// a nonexistent "foo.cc:42" path instead of "foo.cc".
TEST(BuildFileLineUri, IgnoresTrailingDecorativeColonAfterLineNumber) {
  char uri[256];
  const std::string span = "/src/foo.cc:42:";
  EXPECT_TRUE(BuildFileLineUri(span.c_str(), span.size(),
                               /*base_path=*/nullptr, /*host=*/nullptr, uri,
                               sizeof(uri)));
  EXPECT_STREQ("file://localhost/src/foo.cc", uri);
}

TEST(SplitFileLineSpan, SplitsPlainFileLine) {
  const std::string span = "/src/foo.cc:42";
  const char* path;
  std::size_t path_length;
  const char* line;
  std::size_t line_length;
  ASSERT_TRUE(SplitFileLineSpan(span.c_str(), span.size(), &path, &path_length,
                                &line, &line_length));
  EXPECT_EQ(std::string(path, path_length), "/src/foo.cc");
  EXPECT_EQ(std::string(line, line_length), "42");
}

TEST(SplitFileLineSpan, IgnoresTrailingDecorativeColon) {
  const std::string span = "/src/foo.cc:42:";
  const char* path;
  std::size_t path_length;
  const char* line;
  std::size_t line_length;
  ASSERT_TRUE(SplitFileLineSpan(span.c_str(), span.size(), &path, &path_length,
                                &line, &line_length));
  EXPECT_EQ(std::string(path, path_length), "/src/foo.cc");
  EXPECT_EQ(std::string(line, line_length), "42");
}

TEST(SplitFileLineSpan, RejectsMissingLineNumber) {
  const std::string span = "/src/foo.cc";
  const char* path;
  std::size_t path_length;
  const char* line;
  std::size_t line_length;
  EXPECT_FALSE(SplitFileLineSpan(span.c_str(), span.size(), &path, &path_length,
                                 &line, &line_length));
}

TEST(SplitFileLineSpan, RejectsMissingSeparator) {
  const std::string span = "42";
  const char* path;
  std::size_t path_length;
  const char* line;
  std::size_t line_length;
  EXPECT_FALSE(SplitFileLineSpan(span.c_str(), span.size(), &path, &path_length,
                                 &line, &line_length));
}

TEST(SplitFileLineSpan, RejectsEmptyPath) {
  const std::string span = ":42";
  const char* path;
  std::size_t path_length;
  const char* line;
  std::size_t line_length;
  EXPECT_FALSE(SplitFileLineSpan(span.c_str(), span.size(), &path, &path_length,
                                 &line, &line_length));
}

TEST(FormatDisplayPath, RelativeToCwd) {
  char out[128];
  const std::string path = "/home/user/project/src/foo.cc";
  FormatDisplayPath(path.c_str(), path.size(), "/home/user/project",
                    /*prefix_components=*/2, /*suffix_components=*/2, out,
                    sizeof(out));
  EXPECT_STREQ("src/foo.cc", out);
}

TEST(FormatDisplayPath, RelativeToRootCwdKeepsFirstComponent) {
  char out[128];
  const std::string path = "/foo.cc";
  FormatDisplayPath(path.c_str(), path.size(), "/",
                    /*prefix_components=*/2, /*suffix_components=*/2, out,
                    sizeof(out));
  EXPECT_STREQ("foo.cc", out);
}

TEST(FormatDisplayPath, ShortPathUnchanged) {
  char out[128];
  const std::string path = "/usr/foo.h";
  FormatDisplayPath(path.c_str(), path.size(), /*cwd=*/nullptr,
                    /*prefix_components=*/2, /*suffix_components=*/2, out,
                    sizeof(out));
  EXPECT_STREQ("/usr/foo.h", out);
}

TEST(FormatDisplayPath, CompactsOutOfTreePathWithPrefixAndSuffix) {
  char out[128];
  const std::string path = "/usr/include/x86_64-linux-gnu/c++/16/bits/mutex.h";
  FormatDisplayPath(path.c_str(), path.size(), "/home/user/project",
                    /*prefix_components=*/2, /*suffix_components=*/2, out,
                    sizeof(out));
  EXPECT_STREQ("/usr/include/.../bits/mutex.h", out);
}

TEST(FormatDisplayPath, PrefixAndSuffixDoNotOverlap) {
  char out[128];
  // Exactly 3 components ("aaa...", "bbb...", "ccc....h"): a 2-component
  // prefix and a 2-component suffix would each have to include the
  // middle "bbb..." component, so it is dropped instead of being shown
  // (and colored) twice.
  const std::string path =
      "/aaaaaaaaaaaaaaaaaaaa/bbbbbbbbbbbbbbbbbbbb/cccccccccc.h";
  FormatDisplayPath(path.c_str(), path.size(), /*cwd=*/nullptr,
                    /*prefix_components=*/2, /*suffix_components=*/2, out,
                    sizeof(out));
  EXPECT_THAT(std::string(out), Not(HasSubstr("aaa")));
  // The exact match below also confirms the shared "bbb..." component
  // was not duplicated between the prefix and the suffix.
  EXPECT_STREQ(".../bbbbbbbbbbbbbbbbbbbb/cccccccccc.h", out);
}

TEST(FormatDisplayPath, ComponentCountsAreAdjustable) {
  char out[128];
  const std::string path = "/usr/include/x86_64-linux-gnu/c++/16/bits/mutex.h";
  FormatDisplayPath(path.c_str(), path.size(), /*cwd=*/nullptr,
                    /*prefix_components=*/1, /*suffix_components=*/1, out,
                    sizeof(out));
  EXPECT_STREQ("/usr/.../mutex.h", out);
}

TEST(FormatDisplayPath, ZeroPrefixComponentsOmitsPrefix) {
  char out[128];
  const std::string path = "/usr/include/x86_64-linux-gnu/c++/16/bits/mutex.h";
  FormatDisplayPath(path.c_str(), path.size(), /*cwd=*/nullptr,
                    /*prefix_components=*/0, /*suffix_components=*/2, out,
                    sizeof(out));
  EXPECT_STREQ(".../bits/mutex.h", out);
}

TEST(FormatDisplayPath, TooFewComponentsForPrefixShowsWholeRemainder) {
  char out[128];
  // A single component (no internal separator at all): there is nothing
  // to split into a disjoint prefix and suffix, so the whole thing (minus
  // the leading "/") is shown after the ellipsis, same as the
  // too-few-components fallback used when the prefix and suffix would
  // otherwise overlap.
  const std::string path = "/" + std::string(44, 'a');
  FormatDisplayPath(path.c_str(), path.size(), /*cwd=*/nullptr,
                    /*prefix_components=*/2, /*suffix_components=*/2, out,
                    sizeof(out));
  const std::string expected = ".../" + std::string(44, 'a');
  EXPECT_STREQ(expected.c_str(), out);
}

TEST(FormatDisplayPath, NoSeparatorAtAllShowsWholePath) {
  char out[128];
  // No leading "/" and no internal separator either (not a realistic
  // absolute path, but FormatDisplayPath must still degrade gracefully
  // rather than misbehave on it).
  const std::string path(45, 'b');
  FormatDisplayPath(path.c_str(), path.size(), /*cwd=*/nullptr,
                    /*prefix_components=*/2, /*suffix_components=*/2, out,
                    sizeof(out));
  const std::string expected = ".../" + path;
  EXPECT_STREQ(expected.c_str(), out);
}

TEST(FormatDisplayPath, EmptyPathProducesEmptyResult) {
  char out[128];
  FormatDisplayPath("", 0, /*cwd=*/nullptr, /*prefix_components=*/2,
                    /*suffix_components=*/2, out, sizeof(out));
  EXPECT_STREQ("", out);
}

TEST(FormatDisplayPath, TruncatesToFitSmallBuffer) {
  char out[8];
  const std::string path = "/usr/include/x86_64-linux-gnu/c++/16/bits/mutex.h";
  FormatDisplayPath(path.c_str(), path.size(), /*cwd=*/nullptr,
                    /*prefix_components=*/2, /*suffix_components=*/2, out,
                    sizeof(out));
  // Never overflows the destination buffer, and always NUL-terminates.
  EXPECT_LT(std::strlen(out), sizeof(out));
}

TEST(FormatSymbolizedFrame, PlacesWindowsFileLineBeforeFunction) {
  constexpr char kFunction[] = "nglog::LogMessage::Fail";
  constexpr char kPath[] = "D:\\a\\ng-log\\ng-log\\src\\logging.cc:2224";
  constexpr char kSymbol[] =
      "nglog::LogMessage::Fail (D:\\a\\ng-log\\ng-log\\src\\logging.cc:2224)";
  char out[128];
  const std::size_t file_line_offset = sizeof(kFunction) + 1;
  const std::size_t file_line_length = sizeof(kPath) - 1;

  FormatSymbolizedFrame(kSymbol, sizeof(kSymbol) - 1, file_line_offset,
                        file_line_length, "D:\\a\\ng-log\\ng-log", out,
                        sizeof(out));

  EXPECT_STREQ("src\\logging.cc:2224 nglog::LogMessage::Fail", out);
}

TEST(FormatSymbolizedFrame, KeepsPosixFileLineBeforeFunction) {
  constexpr char kSymbol[] = "/home/project/src/logging.cc:2224 function()";
  char out[128];
  constexpr std::size_t kFileLineLength =
      sizeof("/home/project/src/logging.cc:2224") - 1;

  FormatSymbolizedFrame(kSymbol, sizeof(kSymbol) - 1, /*file_line_offset=*/0,
                        kFileLineLength, "/home/project", out, sizeof(out));

  EXPECT_STREQ("src/logging.cc:2224 function()", out);
}

TEST(FormatDisplayPath, LeavesZeroSizedOutputUntouched) {
  char out[1] = {'x'};
  const std::string path = "/a/very/long/path/to/file.cc";
  FormatDisplayPath(path.c_str(), path.size(), /*cwd=*/nullptr,
                    /*prefix_components=*/1, /*suffix_components=*/1, out, 0);
  EXPECT_EQ(out[0], 'x');
}

TEST(SourceLocation, CachesHostAndWorkingDirectory) {
  EXPECT_EQ(&CachedHostname(), &CachedHostname());
  EXPECT_EQ(&CachedCwd(), &CachedCwd());
}

TEST(StyledOutput, WritesRawText) {
  WriteRawToStderr("");
  SUCCEED();
}

TEST(StyledOutput, ResetsAttributesBeforeLineEndings) {
  const std::string text = "first\nsecond";
  const auto write_content = [](const char* value, std::size_t length) {
    fwrite(value, length, 1, stderr);
  };

  CaptureTestStderr();
  WriteStyledField(ColorSpec{Color::kCyan, TextStyle::kBold, Color::kDefault},
                   ColorMode::kAnsi, write_content, text.data(), text.size());
  const std::string output = GetCapturedTestStderr();

  EXPECT_EQ(output, "\033[1;36mfirst\033[0m\n\033[1;36msecond\033[0m");
}

// Exercises the real environment-backed code path (as opposed to the pure
// ShouldColorize()/ShouldEnableHyperlinks() logic tested above) to make
// sure it runs to completion. The result depends on whatever environment
// happens to run the test (e.g. whether stdout/stderr are a real terminal),
// so nothing is asserted about it beyond that both calls agree with each
// other run to run (the result is cached after the first call).
TEST(StreamSupportsColor, StableAcrossRepeatedCalls) {
  EXPECT_EQ(StreamSupportsColor(stdout), StreamSupportsColor(stdout));
  EXPECT_EQ(StreamSupportsColor(stderr), StreamSupportsColor(stderr));
  EXPECT_EQ(StreamSupportsHyperlinks(stdout), StreamSupportsHyperlinks(stdout));
  EXPECT_EQ(StreamSupportsHyperlinks(stderr), StreamSupportsHyperlinks(stderr));
}

#ifdef NGLOG_OS_EMSCRIPTEN
TEST(Emscripten, ReadsNodeEnvironment) {
  EM_ASM({
    process.env.VTE_VERSION = "8401";
    delete process.env.NO_COLOR;
  });
  EXPECT_TRUE(StreamSupportsHyperlinks(stderr));
}
#endif

struct AppendOnlyFormatter {
  std::string text;
  void AppendString(const char* str) { text += str; }
};

TEST(WithColor, WrapsBodyWhenEnabled) {
  AppendOnlyFormatter formatter;
  WithColor(formatter,
            ColorSpec{Color::kRed, TextStyle::kNone, Color::kDefault}, true,
            [&formatter] { formatter.AppendString("x"); });
  EXPECT_EQ(formatter.text, "\033[31mx\033[0m");
}

TEST(WithColor, SkipsEscapeCodesWhenDisabled) {
  AppendOnlyFormatter formatter;
  WithColor(formatter,
            ColorSpec{Color::kRed, TextStyle::kNone, Color::kDefault}, false,
            [&formatter] { formatter.AppendString("x"); });
  EXPECT_EQ(formatter.text, "x");
}

TEST(Hyperlink, WrapsBodyWhenUriIsPresent) {
  AppendOnlyFormatter formatter;
  Hyperlink("file:///a").Wrap(formatter, [&formatter] {
    formatter.AppendString("x");
  });
  EXPECT_EQ(formatter.text, "\033]8;;file:///a\033\\x\033]8;;\033\\");
}

TEST(Hyperlink, SkipsWhenUriIsMissing) {
  AppendOnlyFormatter formatter;
  Hyperlink(nullptr).Wrap(formatter,
                          [&formatter] { formatter.AppendString("x"); });
  EXPECT_EQ(formatter.text, "x");
}

TEST(Hyperlink, UriReflectsConstructorArguments) {
  const Hyperlink hyperlink("file:///a");
  EXPECT_STREQ("file:///a", hyperlink.uri());

  const Hyperlink null_hyperlink(nullptr);
  EXPECT_EQ(null_hyperlink.uri(), nullptr);

  const Hyperlink default_constructed;
  EXPECT_EQ(default_constructed.uri(), nullptr);
}

}  // namespace

int main(int argc, char** argv) {
  InitializeLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);
#ifdef NGLOG_USE_GFLAGS
  ParseCommandLineFlags(&argc, &argv, true);
#endif

  return RUN_ALL_TESTS();
}
