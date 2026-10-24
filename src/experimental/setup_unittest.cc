// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "ng-log/experimental/setup.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "config.h"
#include "ng-log/experimental/attributes.h"
#include "ng-log/experimental/log.h"
#include "ng-log/experimental/sink.h"
#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "ng-log/platform.h"
#include "setup_module_test_helper.h"
#include "testing.h"
#include "testing_utilities.h"
#include "utilities.h"

#if defined(NGLOG_OS_WINDOWS)
#  include <io.h>
#else
#  include <unistd.h>
#endif

namespace {

using namespace ngx_testing;
using testing::ElementsAre;
using testing::ElementsAreArray;
using testing::HasSubstr;
using testing::IsEmpty;
using testing::Not;
using testing::StartsWith;

// Returns the contents of a file, or an empty string if it does not exist. Text
// mode turns the line endings of Windows into newlines.
std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file{path};
  return {std::istreambuf_iterator<char>{file},
          std::istreambuf_iterator<char>{}};
}

// Writes log files named exactly after their base file name into the
// temporary directory of the test.
class ExperimentalSetup : public ExperimentalLogging {
 protected:
  void SetUp() override {
    ExperimentalLogging::SetUp();
    nglog::SetFlag(FLAGS_logtostderr, false);
    nglog::SetFlag(FLAGS_timestamp_in_logfile_name, false);
    nglog::SetFlag(FLAGS_log_dir, nglog::TestTmpDir());
  }

  void TearDown() override {
    // Closes the log files, so that they can be removed on Windows as well.
    nglog::LogToStderr();
    for (const std::filesystem::path& path : created_) {
      std::filesystem::remove(path);
    }
  }

  // Path of a file in the temporary directory that the test removes.
  std::filesystem::path Created(std::string_view name) {
    created_.push_back(std::filesystem::path{nglog::TestTmpDir()} / name);
    std::filesystem::remove(created_.back());
    return created_.back();
  }

 private:
  std::vector<std::filesystem::path> created_;
};

TEST_F(ExperimentalSetup, InitializeCopiesBaseNameOfProgram) {
  static const std::string previous = nglog::ProgramInvocationShortName();
  ngx::Shutdown();
  const bool shut_down = !ngx::IsInitialized();
  std::string program = "/usr/bin/program_with_suffix";
  constexpr std::string_view kSuffix = "_with_suffix";
  ngx::Initialize(
      std::string_view{program}.substr(0, program.size() - kSuffix.size()));
  const bool initialized = ngx::IsInitialized();
  program.assign(program.size(), 'x');
  const std::string name = nglog::ProgramInvocationShortName();
  if (nglog::IsLoggingInitialized()) {
    nglog::ShutdownLogging();
  }
  nglog::InitializeLogging(previous.c_str());
  EXPECT_TRUE(shut_down);
  EXPECT_TRUE(initialized);
  EXPECT_EQ(name, "program");
}

TEST_F(ExperimentalSetup, SetVerbositySetsDefaultLevel) {
  constexpr int kLevel = 2;
  ngx::SetVerbosity(kLevel);
  EXPECT_EQ(nglog::GetFlag(FLAGS_v), kLevel);
  EXPECT_TRUE(ngx::IsEnabled(ngx::Verbose{kLevel}));
  EXPECT_FALSE(ngx::IsEnabled(ngx::Verbose{kLevel + 1}));
}

// The pattern is followed by characters that are not part of it.
TEST_F(ExperimentalSetup, SetVerbositySetsModuleLevel) {
  constexpr int kLevel = 1;
  const std::string text = std::string{kSetupTestModule} + "_and_more";
  const std::string_view pattern =
      std::string_view{text}.substr(0, kSetupTestModule.size());
  ASSERT_FALSE(IsVerboseInSetupTestModule(kLevel));
  const int previous = ngx::SetVerbosity(pattern, kLevel);
  const bool enabled = IsVerboseInSetupTestModule(kLevel);
  const int replaced = ngx::SetVerbosity(pattern, previous);
  EXPECT_EQ(previous, 0);
  EXPECT_TRUE(enabled);
  EXPECT_EQ(replaced, kLevel);
  EXPECT_FALSE(IsVerboseInSetupTestModule(kLevel));
}

TEST_F(ExperimentalSetup, SetVerbosityReportsPatternOfLength) {
  const std::string text = std::string{kSetupTestModule} + "_and_more";
  const std::string_view pattern =
      std::string_view{text}.substr(0, kSetupTestModule.size());
  nglog::SetFlag(FLAGS_logtostderr, true);
  ngx::SetVerbosity(1);
  int previous = 0;
  const Output output = Capture(
      [pattern, &previous] { previous = ngx::SetVerbosity(pattern, 0); });
  ngx::SetVerbosity(0);
  ngx::SetVerbosity(pattern, previous);
  EXPECT_THAT(
      output.stderr_text,
      HasSubstr(std::format("Set VLOG level for \"{}\" to 0", pattern)));
}

TEST_F(ExperimentalSetup, SetStderrThresholdSetsFlag) {
  ngx::SetStderrThreshold(ngx::Warning);
  EXPECT_EQ(nglog::GetFlag(FLAGS_stderrthreshold),
            static_cast<std::int32_t>(NGLOG_WARNING));
}

TEST_F(ExperimentalSetup, LogOnlyToStderrStopsWritingLogFiles) {
  const std::filesystem::path file = Created("setup_only_stderr");
  ngx::SetLogFile(ngx::Info, file);
  ngx::LogOnlyToStderr();
  const Output output =
      Capture([] { ngx::Log(ngx::Info, "only to the standard error"); });
  ngx::Flush(ngx::Info);
  EXPECT_THAT(output.stderr_text, HasSubstr("only to the standard error"));
  EXPECT_THAT(output.stderr_text, Not(HasSubstr("Could not create log file")));
  EXPECT_EQ(nglog::GetFlag(FLAGS_stderrthreshold),
            static_cast<std::int32_t>(NGLOG_INFO));
  EXPECT_FALSE(std::filesystem::exists(file));
}

TEST_F(ExperimentalSetup, SetLogFileWritesMessages) {
  const std::filesystem::path file = Created("setup_log_file");
  ngx::SetLogFile(ngx::Info, file);
  ngx::Log(ngx::Info, "to the log file");
  ngx::Flush(ngx::Info);
  EXPECT_THAT(ReadFile(file), HasSubstr("to the log file"));
}

TEST_F(ExperimentalSetup, DisableLogFileStopsWritingMessages) {
  const std::filesystem::path file = Created("setup_disabled_file");
  ngx::SetLogFile(ngx::Info, file);
  ngx::Log(ngx::Info, "before disabling");
  ngx::DisableLogFile(ngx::Info);
  ngx::Log(ngx::Info, "after disabling");
  ngx::Flush(ngx::Info);
  const std::string contents = ReadFile(file);
  EXPECT_THAT(contents, HasSubstr("before disabling"));
  EXPECT_THAT(contents, Not(HasSubstr("after disabling")));
}

// The name of a link to a file within the temporary directory.
std::filesystem::path LinkTo(std::string_view name) {
#if defined(NGLOG_OS_WINDOWS)
  constexpr std::string_view kLinkSuffix = ".INFO.lnk";
#else
  constexpr std::string_view kLinkSuffix = ".INFO";
#endif
  return std::filesystem::path{nglog::TestTmpDir()} /
         (std::string{name} + std::string{kLinkSuffix});
}

TEST_F(ExperimentalSetup, SetLogFileLinkNamesLink) {
  constexpr std::string_view kLinkName = "setup_link";
  const std::filesystem::path link = LinkTo(kLinkName);
  std::filesystem::remove(link);
  ngx::SetLogFileLink(ngx::Info, kLinkName);
  ngx::SetLogFile(ngx::Info, Created("setup_linked_file"));
  ngx::Log(ngx::Info, "through the link");
  ngx::Flush(ngx::Info);
  const bool exists = std::filesystem::exists(link);
  nglog::LogToStderr();
  std::filesystem::remove(link);
  ngx::SetLogFileLink(ngx::Info, nglog::ProgramInvocationShortName());
  EXPECT_TRUE(exists);
}

TEST_F(ExperimentalSetup, DisableLogFileLinkStopsLinking) {
  const std::filesystem::path link =
      LinkTo(nglog::ProgramInvocationShortName());
  std::filesystem::remove(link);
  ngx::DisableLogFileLink(ngx::Info);
  const std::filesystem::path file = Created("setup_unlinked_file");
  ngx::SetLogFile(ngx::Info, file);
  ngx::Log(ngx::Info, "without a link");
  ngx::Flush(ngx::Info);
  const bool exists = std::filesystem::exists(link);
  nglog::LogToStderr();
  std::filesystem::remove(link);
  ngx::SetLogFileLink(ngx::Info, nglog::ProgramInvocationShortName());
  EXPECT_THAT(ReadFile(file), HasSubstr("without a link"));
  EXPECT_FALSE(exists);
}

// The suffix is followed by characters that are not part of it.
TEST_F(ExperimentalSetup, SetLogFileSuffixAppendsSuffix) {
  constexpr std::string_view kText = ".port80_and_more";
  constexpr std::string_view kSuffix = kText.substr(0, 7);
  const std::filesystem::path file = Created("setup_suffixed");
  const std::filesystem::path suffixed =
      Created(std::string{"setup_suffixed"} + std::string{kSuffix});
  ngx::SetLogFileSuffix(kSuffix);
  ngx::SetLogFile(ngx::Info, file);
  ngx::Log(ngx::Info, "with a suffix");
  ngx::Flush(ngx::Info);
  ngx::SetLogFileSuffix({});
  EXPECT_THAT(ReadFile(suffixed), HasSubstr("with a suffix"));
}

#if defined(HAVE_SUBPROCESS)
// The addresses are followed by characters that are not part of them.
TEST_F(ExperimentalSetup, SetEmailThresholdSendsMessagesOfSeverity) {
  constexpr std::string_view kText = "user@example.com,other@example.com";
  constexpr std::string_view kAddress = kText.substr(0, 16);
  const std::filesystem::path arguments = Created("setup_mailer_arguments");
  nglog::ScopedEnvironmentVariable recorded{"NGLOG_TEST_MAILER_ARGUMENTS",
                                            arguments.string().c_str()};
  nglog::SetFlag(FLAGS_logmailer, std::string{EMAIL_TEST_HELPER_PATH});
  ngx::SetLogFile(ngx::Info, Created("setup_emailed_file"));
  ngx::SetEmailThreshold(ngx::Warning, kAddress);
  ngx::Log(ngx::Info, "not sent");
  const bool sent_info = std::filesystem::exists(arguments);
  ngx::Log(ngx::Warning, "sent");
  // Restores the default, which sends no message by email.
  nglog::SetEmailLogging(static_cast<nglog::LogSeverity>(nglog::NUM_SEVERITIES),
                         "");
  EXPECT_FALSE(sent_info);
  EXPECT_THAT(ReadFile(arguments), HasSubstr("\nuser@example.com\n"));
}
#endif

TEST_F(ExperimentalSetup, FlushWritesBufferedMessages) {
  constexpr std::int32_t kBufferSeconds = 3600;
  nglog::SetFlag(FLAGS_logbufsecs, kBufferSeconds);
  nglog::SetFlag(FLAGS_logbuflevel, static_cast<std::int32_t>(NGLOG_INFO));
  const std::filesystem::path file = Created("setup_flushed_file");
  ngx::SetLogFile(ngx::Info, file);
  ngx::Log(ngx::Info, "first message");
  ngx::Log(ngx::Info, "buffered message");
  const std::string before = ReadFile(file);
  ngx::Flush(ngx::Info);
  EXPECT_THAT(before, Not(HasSubstr("buffered message")));
  EXPECT_THAT(ReadFile(file), HasSubstr("buffered message"));
}

TEST(ExperimentalSetupNames, NameMatchesPrefix) {
  EXPECT_EQ(ngx::Name(ngx::Info), "INFO");
  EXPECT_EQ(ngx::Name(ngx::Warning), "WARNING");
  EXPECT_EQ(ngx::Name(ngx::Error), "ERROR");
  EXPECT_EQ(ngx::Name(ngx::Fatal), "FATAL");
}

TEST(ExperimentalSetupDirectories, LogDirectoriesMatchMacros) {
  std::vector<std::filesystem::path> expected;
  for (const std::string& directory : nglog::GetLoggingDirectories()) {
    expected.emplace_back(directory);
  }
  ASSERT_THAT(expected, Not(IsEmpty()));
  EXPECT_THAT(ngx::LogDirectories(), ElementsAreArray(expected));
}

// The fingerprint is followed by characters that are not part of it.
TEST_F(ExperimentalSetup, SetFingerprintAppearsInHeader) {
  constexpr std::string_view kText = "build 1234 and more";
  constexpr std::string_view kFingerprint = kText.substr(0, 10);
  const std::filesystem::path file = Created("setup_fingerprinted_file");
  ngx::SetFingerprint(kFingerprint);
  ngx::SetLogFile(ngx::Info, file);
  ngx::Log(ngx::Info, "fingerprinted");
  ngx::Flush(ngx::Info);
  ngx::SetFingerprint({});
  EXPECT_THAT(ReadFile(file),
              HasSubstr("Application fingerprint: build 1234\n"));
}

[[noreturn]] void ThrowFromFatalHandler() {
  throw std::logic_error{"fatal handler"};
}

TEST(ExperimentalSetupFatalHandler, SetFatalHandlerReturnsPrevious) {
  const ngx::FatalHandler previous =
      ngx::SetFatalHandler(&ThrowFromFatalHandler);
  const ngx::FatalHandler replaced = ngx::SetFatalHandler(previous);
  EXPECT_NE(previous, nullptr);
  EXPECT_EQ(replaced, &ThrowFromFatalHandler);
}

// Records the messages a structured sink receives.
class RecordingStructuredSink final : public ngx::Sink {
 public:
  void Send(const ngx::Record& record) override {
    texts.emplace_back(record.text());
    tags.emplace_back(record.tag() != nullptr ? record.tag() : "");
    lines.push_back(record.location().line());
  }

  std::vector<std::string> texts;
  std::vector<std::string> tags;
  std::vector<std::uint_least32_t> lines;
};

TEST_F(ExperimentalLogging, AddSinkReceivesMessagesOfTheFunctions) {
  RecordingStructuredSink sink;
  std::uint_least32_t line = 0;
  Capture([&sink, &line] {
    ngx::AddSink(sink);
    // clang-format off
    ngx::Log(ngx::Info | ngx::Tag{"net"}, "from the function"); line = std::source_location::current().line();
    // clang-format on
    LOG(INFO) << "from the macro";
    ngx::RemoveSink(sink);
    ngx::Log(ngx::Info, "after removal");
  });
  EXPECT_THAT(sink.texts, ElementsAre("from the function"));
  EXPECT_THAT(sink.tags, ElementsAre("net"));
  EXPECT_THAT(sink.lines, ElementsAre(line));
}

TEST_F(ExperimentalLogging, AddSinkSkipsMessagesOnlyForAnotherSink) {
  RecordingStructuredSink sink;
  RecordingStructuredSink other;
  Capture([&sink, &other] {
    ngx::AddSink(sink);
    ngx::Log(ngx::Info | ngx::ToSink{&other} | ngx::SkipLogFile, "only other");
    ngx::Log(ngx::Info, "to the log");
    ngx::RemoveSink(sink);
  });
  EXPECT_THAT(sink.texts, ElementsAre("to the log"));
  EXPECT_THAT(other.texts, ElementsAre("only other"));
}

TEST_F(ExperimentalLogging, AddSinkReceivesAsyncMessages) {
  RecordingStructuredSink sink;
  Capture([&sink] {
    ngx::AddSink(sink);
    ngx::Log(ngx::Async | ngx::Info | ngx::Tag{"deferred"},
             "from the logging thread");
    ngx::Flush(ngx::Info);
    ngx::RemoveSink(sink);
  });
  EXPECT_THAT(sink.texts, ElementsAre("from the logging thread"));
  EXPECT_THAT(sink.tags, ElementsAre("deferred"));
}

#ifdef GTEST_HAS_DEATH_TEST
using ExperimentalSetupDeathTest = ExperimentalSetup;

TEST_F(ExperimentalSetupDeathTest, InitializeRejectsNullCharacter) {
  constexpr std::string_view kProgram{"pro\0gram", 8};
  EXPECT_DEATH(ngx::Initialize(kProgram),
               "program name must not contain a null character, but "
               "the character at index 3 of 8 is one");
}

TEST_F(ExperimentalSetupDeathTest, SetLogFileRejectsEmptyName) {
  EXPECT_DEATH(ngx::SetLogFile(ngx::Info, std::filesystem::path{}),
               "base file name must not be empty. DisableLogFile\\(\\) stops "
               "writing a log file");
}

TEST_F(ExperimentalSetupDeathTest, SetLogFileRejectsNullCharacter) {
  const std::filesystem::path file{std::string{"setup\0file", 10}};
  EXPECT_DEATH(ngx::SetLogFile(ngx::Info, file),
               "base file name must not contain a null character, but "
               "the character at index 5 of 10 is one");
}

TEST_F(ExperimentalSetupDeathTest, SetLogFileLinkRejectsEmptyName) {
  EXPECT_DEATH(ngx::SetLogFileLink(ngx::Info, std::filesystem::path{}),
               "link name must be a file name without directories, but it is "
               "''");
}

TEST_F(ExperimentalSetupDeathTest, SetLogFileLinkRejectsNullCharacter) {
  const std::filesystem::path link{std::string{"a\0b", 3}};
  EXPECT_DEATH(ngx::SetLogFileLink(ngx::Info, link),
               "link name must not contain a null character, but the "
               "character at index 1 of 3 is one");
}

TEST_F(ExperimentalSetupDeathTest, SetLogFileLinkRejectsDirectories) {
  EXPECT_DEATH(ngx::SetLogFileLink(ngx::Info, std::filesystem::path{"a/b"}),
               "link name must be a file name without directories, but it is "
               "'a.b'");
}

TEST_F(ExperimentalSetupDeathTest, SetLogFileSuffixRejectsNullCharacter) {
  constexpr std::string_view kSuffix{".a\0b", 4};
  EXPECT_DEATH(ngx::SetLogFileSuffix(kSuffix),
               "log file suffix must not contain a null character, but "
               "the character at index 2 of 4 is one");
}

TEST_F(ExperimentalSetupDeathTest, SetEmailThresholdRejectsNullCharacter) {
  constexpr std::string_view kAddresses{"a@b\0c", 5};
  EXPECT_DEATH(ngx::SetEmailThreshold(ngx::Error, kAddresses),
               "email addresses must not contain a null character, but "
               "the character at index 3 of 5 is one");
}

constexpr int kFatalHandlerExitCode = 3;

[[noreturn]] void ExitFromFatalHandler() { std::_Exit(kFatalHandlerExitCode); }

TEST(ExperimentalSetupFatalHandlerDeathTest, FatalMessageCallsHandler) {
  EXPECT_EXIT(
      {
        ngx::SetFatalHandler(&ExitFromFatalHandler);
        ngx::Log(ngx::Fatal, "calls the handler");
      },
      testing::ExitedWithCode(kFatalHandlerExitCode), "calls the handler");
}

TEST(ExperimentalSetupFatalHandlerDeathTest, SetFatalHandlerRejectsNull) {
  EXPECT_DEATH(ngx::SetFatalHandler(nullptr), "fatal handler must not be null");
}

#  if defined(HAVE_STACKTRACE) && defined(HAVE_SYMBOLIZE)
// Precedes every chunk of the failure dump the writer receives.
constexpr std::string_view kFailureWriterMarker = "experimental writer: ";

void WriteToStderr(std::string_view data) {
#    if defined(NGLOG_OS_WINDOWS)
  static_cast<void>(
      _write(_fileno(stderr), data.data(), static_cast<unsigned>(data.size())));
#    else
  static_cast<void>(write(fileno(stderr), data.data(), data.size()));
#    endif
}

void WriteWithMarker(std::string_view data) {
  WriteToStderr(kFailureWriterMarker);
  WriteToStderr(data);
}

TEST(ExperimentalSetupFailureWriterDeathTest, WriterReceivesDump) {
  EXPECT_DEATH(
      {
        nglog::InstallFailureSignalHandler();
        ngx::SetFailureWriter(&WriteWithMarker);
        std::abort();
      },
      "experimental writer: \\*\\*\\* Aborted at ");
}
#  endif

TEST(ExperimentalSetupFailureWriterDeathTest, SetFailureWriterRejectsNull) {
  EXPECT_DEATH(ngx::SetFailureWriter(nullptr),
               "failure writer must not be null");
}

using ExperimentalLoggingDeathTest = ExperimentalLogging;

TEST_F(ExperimentalLoggingDeathTest, AddSinkRejectsSinkAddedTwice) {
  RecordingStructuredSink sink;
  EXPECT_DEATH(
      {
        ngx::AddSink(sink);
        ngx::AddSink(sink);
      },
      "sink was added already");
}

TEST_F(ExperimentalLoggingDeathTest, RemoveSinkRejectsSinkNotAdded) {
  RecordingStructuredSink sink;
  EXPECT_DEATH(ngx::RemoveSink(sink), "sink was not added");
}
#endif  // GTEST_HAS_DEATH_TEST

#if defined(HAVE_STACKTRACE) && defined(HAVE_SYMBOLIZE)
NGLOG_ATTRIBUTE_NOINLINE std::string StackTraceOfHelper() {
  std::string trace = ngx::StackTrace();
  NGLOG_BLOCK_TAIL_CALL_OPTIMIZATION();
  return trace;
}

TEST(ExperimentalSetupStackTrace, StartsAtCaller) {
  const std::string trace = StackTraceOfHelper();
  const std::string first_frame = trace.substr(0, trace.find('\n'));
  EXPECT_THAT(first_frame, HasSubstr("StackTraceOfHelper"));
}
#elif defined(GTEST_HAS_DEATH_TEST)
TEST(ExperimentalSetupStackTraceDeathTest, IsUnsupported) {
  EXPECT_DEATH(static_cast<void>(ngx::StackTrace()),
               "stack traces are not supported on this platform");
}
#endif

}  // namespace
