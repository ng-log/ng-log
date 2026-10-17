// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
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
#include "internal/utf8.h"
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

using namespace nglog;
using namespace nglog::internal;
using testing::IsEmpty;
using testing::IsNull;
using testing::NotNull;
#ifdef NGLOG_OS_WINDOWS
using testing::ElementsAre;
using testing::StrEq;
#endif

namespace {

TEST(Flags, SymbolizeFileBasePathIgnoresEnvironment) {
  EXPECT_THAT(nglog::GetFlag(FLAGS_symbolize_file_base_path), IsEmpty());
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

constexpr std::size_t kAnsiSequenceBufferSize = 32;

struct AnsiSequenceCase {
  const char* name;
  ColorSpec spec;
  const char* expected;
};

class FormatAnsiSequenceTest : public testing::TestWithParam<AnsiSequenceCase> {
};

TEST_P(FormatAnsiSequenceTest, ProducesExpectedSequence) {
  char buf[kAnsiSequenceBufferSize];
  GetParam().spec.FormatAnsiSequence(buf, sizeof(buf));
  EXPECT_STREQ(GetParam().expected, buf);
}

INSTANTIATE_TEST_SUITE_P(
    Styles, FormatAnsiSequenceTest,
    testing::Values(
        AnsiSequenceCase{
            "Foreground",
            ColorSpec{Color::kRed, TextStyle::kNone, Color::kDefault},
            "\033[31m"},
        AnsiSequenceCase{
            "BrightForeground",
            ColorSpec{Color::kGray, TextStyle::kNone, Color::kDefault},
            "\033[90m"},
        AnsiSequenceCase{
            "ForegroundAndStyle",
            ColorSpec{Color::kGreen, TextStyle::kBold, Color::kDefault},
            "\033[1;32m"},
        AnsiSequenceCase{
            "Background",
            ColorSpec{Color::kWhite, TextStyle::kNone, Color::kBlue},
            "\033[37;44m"},
        AnsiSequenceCase{
            "Rgb",
            ColorSpec{RgbColor{255, 128, 0}, TextStyle::kNone, Color::kDefault},
            "\033[38;2;255;128;0m"},
        AnsiSequenceCase{"CombinedTextStyles",
                         ColorSpec{Color::kDefault,
                                   TextStyle::kDim | TextStyle::kItalic |
                                       TextStyle::kUnderline,
                                   Color::kDefault},
                         "\033[2;3;4;39m"}),
    CaseName{});

TEST(FormatAnsiSequence, NeverOverflowsAndAlwaysTerminates) {
  constexpr std::size_t kTooSmallBufferSize = 6;
  char buf[kTooSmallBufferSize];
  ColorSpec{Color::kGreen, TextStyle::kBold, Color::kDefault}
      .FormatAnsiSequence(buf, sizeof(buf));
  // Whatever got written must be NUL-terminated within bounds.
  EXPECT_THAT(std::memchr(buf, '\0', sizeof(buf)), NotNull());
}

TEST(FormatAnsiSequence, HandlesSmallBuffers) {
  char buf[1] = {'x'};
  ColorSpec{Color::kDefault, TextStyle::kNone, Color::kDefault}
      .FormatAnsiSequence(buf, 0);
  EXPECT_EQ(buf[0], 'x');

  ColorSpec{Color::kDefault, TextStyle::kNone, Color::kDefault}
      .FormatAnsiSequence(buf, sizeof(buf));
  EXPECT_EQ(buf[0], '\0');
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

TEST(Theme, DefaultThemeUsesWhiteModuleHelpRoles) {
  const Theme& theme = DefaultTheme();
  EXPECT_EQ(theme.Get(Role::kHelpModuleHeader).foreground, Color::kWhite);
  EXPECT_EQ(theme.Get(Role::kHelpModuleHeader).style, TextStyle::kNone);
  EXPECT_EQ(theme.Get(Role::kHelpModuleName).foreground, Color::kWhite);
  EXPECT_EQ(theme.Get(Role::kHelpModuleName).style, TextStyle::kBold);
}

TEST(Theme, SetAndGetRoundTrip) {
  Theme theme;
  const ColorSpec spec{Color::kBrightMagenta, TextStyle::kUnderline,
                       Color::kDefault};
  theme.Set(Role::kStackFunction, spec);
  EXPECT_EQ(theme.Get(Role::kStackFunction), spec);
}

constexpr std::size_t kUriBufferSize = 256;

TEST(BuildFileUriWithoutLine, AcceptsPosixAbsolutePath) {
  char uri[kUriBufferSize];
  const std::string path = "/src/foo.cc";
  ASSERT_TRUE(BuildFileUri(path.c_str(), path.size(), /*base_path=*/nullptr,
                           /*host=*/nullptr, uri, sizeof(uri)));
  EXPECT_STREQ("file://localhost/src/foo.cc", uri);
}

struct FileLineUriCase {
  const char* name;
  const char* span;
  const char* base_path;
  const char* host;
  const char* expected;
};

class BuildFileLineUriAccepts : public testing::TestWithParam<FileLineUriCase> {
};

TEST_P(BuildFileLineUriAccepts, ProducesFileUriWithoutLineNumber) {
  const FileLineUriCase& param = GetParam();
  char uri[kUriBufferSize];
  ASSERT_TRUE(BuildFileLineUri(param.span, std::strlen(param.span),
                               param.base_path, param.host, uri, sizeof(uri)));
  EXPECT_STREQ(param.expected, uri);
}

INSTANTIATE_TEST_SUITE_P(
    Spans, BuildFileLineUriAccepts,
    testing::Values(
        FileLineUriCase{"PosixAbsolutePath", "/src/foo.cc:42", nullptr, nullptr,
                        "file://localhost/src/foo.cc"},
        FileLineUriCase{"WindowsAbsolutePath", "C:\\src\\foo.cc:42", nullptr,
                        nullptr, "file://localhost/C:/src/foo.cc"},
        FileLineUriCase{"EncodesPathCharactersReservedByUris",
                        "/src/a file#part?.cc:42", nullptr, nullptr,
                        "file://localhost/src/a%20file%23part%3F.cc"},
        FileLineUriCase{"EncodesWindowsPathSeparators", "C:\\src\\a file.cc:42",
                        nullptr, nullptr,
                        "file://localhost/C:/src/a%20file.cc"},
        FileLineUriCase{"NormalizesWindowsBasePathSeparators", "src\\foo.cc:42",
                        "C:\\build", nullptr,
                        "file://localhost/C:/build/src/foo.cc"},
        FileLineUriCase{"UsesSuppliedHost", "/src/foo.cc:42", nullptr,
                        "my-host", "file://my-host/src/foo.cc"},
        FileLineUriCase{"ResolvesRelativePathAgainstBasePath", "src/foo.cc:42",
                        "/build", nullptr, "file://localhost/build/src/foo.cc"},
        FileLineUriCase{"DoesNotDuplicateSeparatorWhenJoiningBasePath",
                        "src/foo.cc:42", "/build/", nullptr,
                        "file://localhost/build/src/foo.cc"},
        FileLineUriCase{"IgnoresBasePathWhenSpanIsAlreadyAbsolute",
                        "/src/foo.cc:42", "/build", nullptr,
                        "file://localhost/src/foo.cc"},
        FileLineUriCase{"HandlesRepeatedSeparatorsInAPath",
                        "/src//nested/foo.cc:42", nullptr, nullptr,
                        "file://localhost/src//nested/foo.cc"},
        // The libbacktrace and addr2line symbolize backends keep a
        // decorative ':' after the line number as part of the "file:line"
        // span so it renders in the same color as the rest of the span, as
        // done by DumpStackFrameInfo() in signalhandler.cc. The line number
        // must not leak into the URI because of that trailing ':', or the
        // link opens a nonexistent "foo.cc:42" path instead of "foo.cc".
        FileLineUriCase{"IgnoresTrailingDecorativeColonAfterLineNumber",
                        "/src/foo.cc:42:", nullptr, nullptr,
                        "file://localhost/src/foo.cc"}),
    CaseName{});

class BuildFileLineUriRejects : public testing::TestWithParam<FileLineUriCase> {
};

TEST_P(BuildFileLineUriRejects, ReturnsFalse) {
  const FileLineUriCase& param = GetParam();
  char uri[kUriBufferSize];
  EXPECT_FALSE(BuildFileLineUri(param.span, std::strlen(param.span),
                                param.base_path, param.host, uri, sizeof(uri)));
}

INSTANTIATE_TEST_SUITE_P(
    Spans, BuildFileLineUriRejects,
    testing::Values(FileLineUriCase{"RelativePathWithoutBasePath",
                                    "src/foo.cc:42", nullptr, nullptr, nullptr},
                    FileLineUriCase{"RelativePathWithEmptyBasePath",
                                    "src/foo.cc:42", "", nullptr, nullptr},
                    FileLineUriCase{"RelativeBasePath", "src/foo.cc:42",
                                    "relative/build", nullptr, nullptr},
                    FileLineUriCase{"MissingLineNumber", "/src/foo.cc", nullptr,
                                    nullptr, nullptr}),
    CaseName{});

TEST(BuildFileLineUri, RejectsTooSmallBuffer) {
  constexpr std::size_t kTooSmallBufferSize = 4;
  char uri[kTooSmallBufferSize];
  const std::string span = "/src/foo.cc:42";
  EXPECT_FALSE(BuildFileLineUri(span.c_str(), span.size(),
                                /*base_path=*/nullptr, /*host=*/nullptr, uri,
                                sizeof(uri)));
}

struct SpanCase {
  const char* name;
  const char* span;
};

class SplitFileLineSpanAccepts : public testing::TestWithParam<SpanCase> {};

TEST_P(SplitFileLineSpanAccepts, SplitsPathAndLineNumber) {
  const char* const span = GetParam().span;
  const char* path = nullptr;
  std::size_t path_length = 0;
  const char* line = nullptr;
  std::size_t line_length = 0;
  ASSERT_TRUE(SplitFileLineSpan(span, std::strlen(span), &path, &path_length,
                                &line, &line_length));
  EXPECT_EQ(std::string(path, path_length), "/src/foo.cc");
  EXPECT_EQ(std::string(line, line_length), "42");
}

INSTANTIATE_TEST_SUITE_P(
    Spans, SplitFileLineSpanAccepts,
    testing::Values(SpanCase{"PlainFileLine", "/src/foo.cc:42"},
                    SpanCase{"TrailingDecorativeColon", "/src/foo.cc:42:"}),
    CaseName{});

class SplitFileLineSpanRejects : public testing::TestWithParam<SpanCase> {};

TEST_P(SplitFileLineSpanRejects, ReturnsFalse) {
  const char* const span = GetParam().span;
  const char* path = nullptr;
  std::size_t path_length = 0;
  const char* line = nullptr;
  std::size_t line_length = 0;
  EXPECT_FALSE(SplitFileLineSpan(span, std::strlen(span), &path, &path_length,
                                 &line, &line_length));
}

INSTANTIATE_TEST_SUITE_P(Spans, SplitFileLineSpanRejects,
                         testing::Values(SpanCase{"MissingLineNumber",
                                                  "/src/foo.cc"},
                                         SpanCase{"MissingSeparator", "42"},
                                         SpanCase{"EmptyPath", ":42"}),
                         CaseName{});

constexpr std::size_t kDisplayPathBufferSize = 128;
constexpr std::size_t kSingleComponentLength = 44;
constexpr std::size_t kUnseparatedPathLength = 45;
constexpr char kOutOfTreePath[] =
    "/usr/include/x86_64-linux-gnu/c++/16/bits/mutex.h";

struct DisplayPathCase {
  const char* name;
  std::string path;
  const char* cwd;
  std::size_t prefix_components;
  std::size_t suffix_components;
  std::string expected;
};

class FormatDisplayPathTest : public testing::TestWithParam<DisplayPathCase> {};

TEST_P(FormatDisplayPathTest, ProducesExpectedPath) {
  const DisplayPathCase& param = GetParam();
  char out[kDisplayPathBufferSize];
  FormatDisplayPath(param.path.c_str(), param.path.size(), param.cwd,
                    param.prefix_components, param.suffix_components, out,
                    sizeof(out));
  EXPECT_EQ(out, param.expected);
}

INSTANTIATE_TEST_SUITE_P(
    Paths, FormatDisplayPathTest,
    testing::Values(
        DisplayPathCase{"RelativeToCwd", "/home/user/project/src/foo.cc",
                        "/home/user/project", 2, 2, "src/foo.cc"},
        DisplayPathCase{"RelativeToRootCwdKeepsFirstComponent", "/foo.cc", "/",
                        2, 2, "foo.cc"},
        DisplayPathCase{"ShortPathUnchanged", "/usr/foo.h", nullptr, 2, 2,
                        "/usr/foo.h"},
        DisplayPathCase{"CompactsOutOfTreePathWithPrefixAndSuffix",
                        kOutOfTreePath, "/home/user/project", 2, 2,
                        "/usr/include/.../bits/mutex.h"},
        // Exactly three components: a two-component prefix and a
        // two-component suffix would each have to include the middle
        // component, so the prefix is dropped instead of showing and
        // coloring the middle component twice.
        DisplayPathCase{
            "PrefixAndSuffixDoNotOverlap",
            "/aaaaaaaaaaaaaaaaaaaa/bbbbbbbbbbbbbbbbbbbb/cccccccccc.h", nullptr,
            2, 2, ".../bbbbbbbbbbbbbbbbbbbb/cccccccccc.h"},
        DisplayPathCase{"ComponentCountsAreAdjustable", kOutOfTreePath, nullptr,
                        1, 1, "/usr/.../mutex.h"},
        DisplayPathCase{"ZeroPrefixComponentsOmitsPrefix", kOutOfTreePath,
                        nullptr, 0, 2, ".../bits/mutex.h"},
        // A single component without any internal separator cannot be split
        // into a disjoint prefix and suffix. The whole component without
        // the leading "/" is shown after the ellipsis, the same fallback
        // used when the prefix and suffix would otherwise overlap.
        DisplayPathCase{"TooFewComponentsForPrefixShowsWholeRemainder",
                        "/" + std::string(kSingleComponentLength, 'a'), nullptr,
                        2, 2,
                        ".../" + std::string(kSingleComponentLength, 'a')},
        // Neither a leading "/" nor an internal separator is unrealistic for
        // an absolute path, but the formatting must still degrade
        // gracefully.
        DisplayPathCase{"NoSeparatorAtAllShowsWholePath",
                        std::string(kUnseparatedPathLength, 'b'), nullptr, 2, 2,
                        ".../" + std::string(kUnseparatedPathLength, 'b')},
        DisplayPathCase{"EmptyPathProducesEmptyResult", "", nullptr, 2, 2, ""}),
    CaseName{});

TEST(FormatDisplayPath, TruncatesToFitSmallBuffer) {
  constexpr std::size_t kTooSmallBufferSize = 8;
  char out[kTooSmallBufferSize];
  const std::string path = kOutOfTreePath;
  FormatDisplayPath(path.c_str(), path.size(), /*cwd=*/nullptr,
                    /*prefix_components=*/2, /*suffix_components=*/2, out,
                    sizeof(out));
  // Never overflows the destination buffer, and always NUL-terminates.
  EXPECT_THAT(std::memchr(out, '\0', sizeof(out)), NotNull());
}

TEST(FormatDisplayPath, LeavesZeroSizedOutputUntouched) {
  char out[1] = {'x'};
  const std::string path = "/a/very/long/path/to/file.cc";
  FormatDisplayPath(path.c_str(), path.size(), /*cwd=*/nullptr,
                    /*prefix_components=*/1, /*suffix_components=*/1, out, 0);
  EXPECT_EQ(out[0], 'x');
}

TEST(FormatSymbolizedFrame, PlacesWindowsFileLineBeforeFunction) {
  constexpr char kFunction[] = "nglog::LogMessage::Fail";
  constexpr char kPath[] = "D:\\a\\ng-log\\ng-log\\src\\logging.cc:2224";
  constexpr char kSymbol[] =
      "nglog::LogMessage::Fail (D:\\a\\ng-log\\ng-log\\src\\logging.cc:2224)";
  char out[kDisplayPathBufferSize];
  const std::size_t file_line_offset = sizeof(kFunction) + 1;
  const std::size_t file_line_length = sizeof(kPath) - 1;

  FormatSymbolizedFrame(kSymbol, sizeof(kSymbol) - 1, file_line_offset,
                        file_line_length, "D:\\a\\ng-log\\ng-log", out,
                        sizeof(out));

  EXPECT_STREQ("src\\logging.cc:2224 nglog::LogMessage::Fail", out);
}

TEST(FormatSymbolizedFrame, KeepsPosixFileLineBeforeFunction) {
  constexpr char kSymbol[] = "/home/project/src/logging.cc:2224 function()";
  char out[kDisplayPathBufferSize];
  constexpr std::size_t kFileLineLength =
      sizeof("/home/project/src/logging.cc:2224") - 1;

  FormatSymbolizedFrame(kSymbol, sizeof(kSymbol) - 1, /*file_line_offset=*/0,
                        kFileLineLength, "/home/project", out, sizeof(out));

  EXPECT_STREQ("src/logging.cc:2224 function()", out);
}

TEST(SourceLocation, CachesHostAndWorkingDirectory) {
  EXPECT_EQ(&CachedHostname(), &CachedHostname());
  EXPECT_EQ(&CachedCwd(), &CachedCwd());
}

#ifdef NGLOG_OS_WINDOWS
TEST(SourceLocation, UsesUtf8ComputerName) {
  std::string expected;
  ASSERT_TRUE(GetComputerNameUtf8(&expected));
  EXPECT_EQ(CachedHostname(), expected);
}
#endif

TEST(StyledOutput, WritesRawText) {
  WriteRawToStderr("");
  SUCCEED();
}

TEST(StyledOutput, ResetsAttributesBeforeLineEndings) {
  const std::string text = "first\nsecond";
  const auto write_content = [](const char* value, std::size_t length) {
    fwrite(value, length, 1, stderr);
  };

  ScopedStreamCapture capture{StandardStream::kStderr};
  WriteStyledField(ColorSpec{Color::kCyan, TextStyle::kBold, Color::kDefault},
                   ColorMode::kAnsi, write_content, text.data(), text.size());
  const std::string output = capture.Stop();

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
  // Do not depend on whether stderr is a terminal.
  EM_ASM({
    process.env.VTE_VERSION = "8401";
    process.env.CLICOLOR_FORCE = "1";
    delete process.env.NO_COLOR;
  });
  EXPECT_TRUE(StreamSupportsHyperlinks(stderr));
}
#endif

struct AppendOnlyFormatter {
  std::string text;
  void AppendString(const char* str, std::size_t length) {
    text.append(str, length);
  }
};

struct LegacyFormatter {
  std::string text;
  void AppendString(const char* str) { text.append(str); }
};

TEST(Hyperlink, SupportsLegacyFormatter) {
  LegacyFormatter formatter;
  Hyperlink hyperlink("https://example.com");

  hyperlink.Wrap(formatter, [&formatter] { formatter.AppendString("body"); });

  EXPECT_EQ(formatter.text,
            "\033]8;;https://example.com\033\\body\033]8;;\033\\");
}

TEST(WithColor, WrapsBodyWhenEnabled) {
  AppendOnlyFormatter formatter;
  WithColor(formatter,
            ColorSpec{Color::kRed, TextStyle::kNone, Color::kDefault}, true,
            [&formatter] { formatter.AppendString("x", 1); });
  EXPECT_EQ(formatter.text, "\033[31mx\033[0m");
}

TEST(WithColor, SkipsEscapeCodesWhenDisabled) {
  AppendOnlyFormatter formatter;
  WithColor(formatter,
            ColorSpec{Color::kRed, TextStyle::kNone, Color::kDefault}, false,
            [&formatter] { formatter.AppendString("x", 1); });
  EXPECT_EQ(formatter.text, "x");
}

TEST(Hyperlink, WrapsBodyWhenUriIsPresent) {
  AppendOnlyFormatter formatter;
  Hyperlink("file:///a").Wrap(formatter, [&formatter] {
    formatter.AppendString("x", 1);
  });
  EXPECT_EQ(formatter.text, "\033]8;;file:///a\033\\x\033]8;;\033\\");
}

TEST(Hyperlink, SkipsWhenUriIsMissing) {
  AppendOnlyFormatter formatter;
  Hyperlink(nullptr).Wrap(formatter,
                          [&formatter] { formatter.AppendString("x", 1); });
  EXPECT_EQ(formatter.text, "x");
}

TEST(Hyperlink, UriReflectsConstructorArguments) {
  const Hyperlink hyperlink("file:///a");
  EXPECT_STREQ("file:///a", hyperlink.uri());

  const Hyperlink null_hyperlink(nullptr);
  EXPECT_THAT(null_hyperlink.uri(), IsNull());

  const Hyperlink default_constructed;
  EXPECT_THAT(default_constructed.uri(), IsNull());
}

}  // namespace

int main(int argc, char** argv) {
  InitializeTestLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);
  nglog::ParseCommandLineFlags(&argc, &argv,
                               nglog::FlagParsingMode::kRemoveFlags);

  return RUN_ALL_TESTS();
}
