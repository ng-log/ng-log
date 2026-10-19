// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "ng-log/flags.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <initializer_list>
#include <new>
#include <ostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "config.h"
#include "internal/levenshtein.h"
#include "internal/process_flag_registry.h"
#include "ng-log/platform.h"
#include "testing_utilities.h"

#if !defined(NGLOG_OS_WINDOWS)
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>

extern char** environ;
#endif

NGLOG_DEFINE(bool, test_bool_flag, false, "Test boolean flag.");
NGLOG_DEFINE(std::int32_t, test_int32_flag, 7, "Test signed flag.");
NGLOG_DEFINE(std::uint32_t, test_uint32_flag, 11, "Test unsigned flag.");
NGLOG_DEFINE(std::string, test_string_flag, "default", "Test string flag.");
NGLOG_DEFINE(std::string, test_wrapped_help_flag, "",
             "This help text is long enough to wrap at the default terminal "
             "width for tests.");
NGLOG_DEFINE(std::string, test_literal_help_flag, "",
             "Literal text mentions --not-a-real-option, TERM=dumb, stdout, "
             "and file:line.");

constexpr char kDefaultSourceVariable[] = "NGLOG_TEST_DEFAULT_SOURCE";

std::string DefaultFromEnvironment() {
  const char* const value = std::getenv(kDefaultSourceVariable);
  return value == nullptr ? "unset" : value;
}

NGLOG_DEFINE(std::string, test_environment_default_flag,
             DefaultFromEnvironment(), "Default read from the environment.");

NGLOG_DEFINE(std::string, test_unbroken_help_flag, "",
             "Unbroken_help_word_that_is_longer_than_the_default_terminal_"
             "width_of_eighty_columns\nUnbroken_help_second_paragraph.");

struct CustomFlagValue {
  int value;

  CustomFlagValue& operator=(const char* value_text) {
    value = value_text[0] - '0';
    return *this;
  }
};

struct ConstructibleOnlyFlagValue {
  explicit ConstructibleOnlyFlagValue(int value) : value(value) {}
  explicit ConstructibleOnlyFlagValue(const char* value)
      : value(value[0] - '0') {}

  int value;
};

namespace nglog {

// Parses and formats the single digit values accepted by the test types.
template <typename T>
struct DigitFlagTraits {
  static bool Parse(const std::string& value, T* output, std::string* error) {
    if (value == "7" || value == "9") {
      output->value = value[0] - '0';
      return true;
    }
    *error = "expected 7 or 9";
    return false;
  }

  static std::string ToString(const T& value) {
    return std::to_string(value.value);
  }
};

template <>
struct FlagTraits<CustomFlagValue> : DigitFlagTraits<CustomFlagValue> {
  static const char* TypeName() noexcept { return "custom"; }
};

template <>
struct FlagTraits<ConstructibleOnlyFlagValue>
    : DigitFlagTraits<ConstructibleOnlyFlagValue> {
  static const char* TypeName() noexcept { return "constructible-only"; }
};

// Names the flag in assertion failures instead of dumping its bytes.
void PrintTo(const FlagInfo& info, std::ostream* os) {
  *os << "flag " << info.name;
}

}  // namespace nglog

std::atomic<bool> g_block_next_destruction{false};
std::atomic<bool> g_destruction_entered{false};
std::atomic<bool> g_destruction_released{false};

struct BlockingDestructionValue {
  ~BlockingDestructionValue() {
    if (g_block_next_destruction.exchange(false)) {
      g_destruction_entered = true;
      while (!g_destruction_released) {
        std::this_thread::yield();
      }
    }
  }
};

namespace nglog {

template <>
struct FlagTraits<BlockingDestructionValue> {
  static const char* TypeName() noexcept { return "blocking"; }

  static bool Parse(const std::string&, BlockingDestructionValue*,
                    std::string*) {
    return true;
  }

  static std::string ToString(const BlockingDestructionValue&) {
    return "value";
  }
};

}  // namespace nglog

std::atomic<bool> g_slow_assignment_entered{false};

// Holds the flag value lock for a while during assignment.
struct SlowAssignmentValue {
  SlowAssignmentValue() = default;
  SlowAssignmentValue(const SlowAssignmentValue&) = default;

  SlowAssignmentValue& operator=(const SlowAssignmentValue& other) {
    g_slow_assignment_entered = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    value = other.value;
    return *this;
  }

  int value = 0;
};

namespace nglog {

template <>
struct FlagTraits<SlowAssignmentValue> {
  static const char* TypeName() noexcept { return "slow"; }

  static bool Parse(const std::string&, SlowAssignmentValue*, std::string*) {
    return true;
  }

  static std::string ToString(const SlowAssignmentValue& value) {
    return std::to_string(value.value);
  }
};

}  // namespace nglog

std::atomic<bool> g_slow_formatting_entered{false};
constexpr int kSlowFormattingMilliseconds = 200;

// Holds the registry lock for a while when the registry formats the value.
struct SlowlyFormattedValue {};

namespace nglog {

template <>
struct FlagTraits<SlowlyFormattedValue> {
  static const char* TypeName() noexcept { return "slowly formatted"; }

  static bool Parse(const std::string&, SlowlyFormattedValue*, std::string*) {
    return true;
  }

  static std::string ToString(const SlowlyFormattedValue&) {
    g_slow_formatting_entered = true;
    std::this_thread::sleep_for(
        std::chrono::milliseconds(kSlowFormattingMilliseconds));
    return "value";
  }
};

}  // namespace nglog

int g_fragile_copies = 0;
int g_failing_copy = 0;

// Fails on a chosen copy, as an allocation might.
struct FragileValue {
  FragileValue() = default;
  FragileValue(const FragileValue& other) : value(other.value) {
    if (++g_fragile_copies == g_failing_copy) {
      throw std::runtime_error("copy failed");
    }
  }
  FragileValue& operator=(const FragileValue&) = default;

  int value = 7;
};

namespace nglog {

template <>
struct FlagTraits<FragileValue> {
  static const char* TypeName() noexcept { return "fragile"; }

  static bool Parse(const std::string&, FragileValue*, std::string*) {
    return true;
  }

  static std::string ToString(const FragileValue& value) {
    return std::to_string(value.value);
  }
};

}  // namespace nglog

FragileValue MakeFragileValue() { return FragileValue{}; }

NGLOG_DEFINE(CustomFlagValue, test_custom_flag, CustomFlagValue{7},
             "Test custom flag.");

void WriteLongStyledFlagHelp(nglog::HelpWriter& help) {
  help.Write(
      "This styled help text is deliberately long so wrapping must "
      "continue after ");
  help.Write(nglog::HelpTextStyle::kOptionPrefix, "--");
  help.Write(nglog::HelpTextStyle::kOption, "styled_help_marker");
  help.Write(
      " and keep rendering the remaining explanation on a separate "
      "line.");
}

class RestoreFailureFlag final : public nglog::FlagBase {
 public:
  RestoreFailureFlag() : nglog::FlagBase("restore_failure_flag", __FILE__, "") {
    nglog::RegisterFlag(*this);
  }

  RestoreFailureFlag(const RestoreFailureFlag&) = delete;
  RestoreFailureFlag& operator=(const RestoreFailureFlag&) = delete;
  ~RestoreFailureFlag() { nglog::UnregisterFlag(*this); }

  const char* type_name() const noexcept override { return "string"; }

  bool SetFromString(const std::string&, std::string* error) const override {
    *error = "test restore failure";
    return false;
  }

  std::string CurrentValue() const override { return "value"; }
  std::string DefaultValue() const override { return "value"; }
};

class ReentrantFlag final : public nglog::FlagBase {
 public:
  ReentrantFlag() : nglog::FlagBase("reentrant_flag", __FILE__, "") {
    nglog::RegisterFlag(*this);
  }

  ReentrantFlag(const ReentrantFlag&) = delete;
  ReentrantFlag& operator=(const ReentrantFlag&) = delete;
  ~ReentrantFlag() { nglog::UnregisterFlag(*this); }

  const char* type_name() const noexcept override { return "string"; }

  bool SetFromString(const std::string& value,
                     std::string* error) const override {
    std::string current_value;
    if (!nglog::GetCommandLineOption("test_string_flag", &current_value)) {
      *error = "could not inspect another flag";
      return false;
    }
    value_ = value;
    return true;
  }

  std::string CurrentValue() const override { return value_; }
  std::string DefaultValue() const override { return ""; }

 private:
  mutable std::string value_;
};

NGLOG_DEFINE(std::string, test_styled_help_flag, "", WriteLongStyledFlagHelp);

static_assert(std::is_same<decltype(FLAGS_test_int32_flag),
                           nglog::Flag<std::int32_t>>::value,
              "NGLOG_DEFINE must expose a typed flag object");
static_assert(
    !std::is_convertible<nglog::Flag<std::int32_t>, std::int32_t>::value,
    "Flag objects must require an explicit synchronized read");

namespace {

using nglog::ScopedEnvironmentVariable;

constexpr char kProgramName[] = "flags_unittest";

constexpr int kReadIterations = 1000;
constexpr std::int32_t kFirstWrittenValue = 17;
constexpr std::int32_t kSecondWrittenValue = 23;
constexpr std::int32_t kOriginalValue = 1;
constexpr std::int32_t kTemporaryValue = 2;
constexpr std::int32_t kReplacementValue = 3;

// Owns mutable copies of the arguments of a command line that starts with the
// program name, as the parser expects them.
class CommandLine {
 public:
  CommandLine(std::initializer_list<const char*> arguments) {
    arguments_.emplace_back(kProgramName);
    arguments_.insert(arguments_.end(), arguments.begin(), arguments.end());
    for (std::string& argument : arguments_) {
      pointers_.push_back(&argument[0]);
    }
    pointers_.push_back(nullptr);
    argc_ = static_cast<int>(arguments_.size());
    argv_ = pointers_.data();
  }

  CommandLine(const CommandLine&) = delete;
  CommandLine& operator=(const CommandLine&) = delete;

  std::uint32_t Parse(
      nglog::FlagParsingMode mode = nglog::FlagParsingMode::kRemoveFlags,
      nglog::UnknownFlagMode unknown_mode = nglog::UnknownFlagMode::kError) {
    return nglog::ParseCommandLineFlags(&argc_, &argv_, mode, unknown_mode);
  }

  //! Returns the arguments that follow the program name after parsing.
  std::vector<std::string> Arguments() const {
    return {argv_ + 1, argv_ + argc_};
  }

  //! Returns the argument vector terminator position after parsing.
  const char* Terminator() const { return argv_[argc_]; }

 private:
  std::vector<std::string> arguments_;
  std::vector<char*> pointers_;
  int argc_ = 0;
  char** argv_ = nullptr;
};

std::vector<nglog::FlagInfo> AllFlags() {
  std::vector<nglog::FlagInfo> flags;
  nglog::GetAllFlags(&flags);
  return flags;
}

testing::Matcher<const nglog::FlagInfo&> FlagNamed(const std::string& name) {
  return testing::Field("name", &nglog::FlagInfo::name, name);
}

testing::Matcher<const nglog::FlagInfo&> FlagWithHelp(const std::string& name,
                                                      const std::string& help) {
  return testing::AllOf(FlagNamed(name),
                        testing::Field("help", &nglog::FlagInfo::help, help));
}

MATCHER(StartsWithAnUppercaseLetter, "starts with an uppercase letter") {
  return !arg.empty() && arg.front() >= 'A' && arg.front() <= 'Z';
}

MATCHER(CurrentValueIsTheDefault, "has its default value as current value") {
  return arg.current_value == arg.default_value;
}

std::string PrintedHelp() {
  nglog::ScopedStreamCapture capture{nglog::StandardStream::kStderr};
  nglog::PrintCommandLineFlags();
  return capture.Stop();
}

std::string StripTerminalStyles(const std::string& text) {
  static const std::regex escape("\033\\[[0-9;]*m");
  return std::regex_replace(text, escape, "");
}

// Constructs a flag in place so that a replacement can occupy the address of
// the destroyed original.
class ReplaceableFlag {
 public:
  // Named differently from the base class, whose trivial destructor a call
  // through the name Flag would select.
  using Int32Flag = nglog::DynamicFlag<std::int32_t>;

  ReplaceableFlag(const char* name, std::int32_t value) : name_(name) {
    flag_ = new (storage_) Int32Flag(name_, __FILE__, "Dynamic flag.", value);
  }

  ReplaceableFlag(const ReplaceableFlag&) = delete;
  ReplaceableFlag& operator=(const ReplaceableFlag&) = delete;

  ~ReplaceableFlag() { flag_->~Int32Flag(); }

  Int32Flag& get() noexcept { return *flag_; }

  void Replace(std::int32_t value) {
    flag_->~Int32Flag();
    flag_ = new (storage_) Int32Flag(name_, __FILE__, "Dynamic flag.", value);
  }

 private:
  const char* name_;
  alignas(Int32Flag) unsigned char storage_[sizeof(Int32Flag)];
  Int32Flag* flag_ = nullptr;
};

void WriteGeneratedUsage(nglog::HelpWriter& help) {
  help.Write(nglog::HelpTextStyle::kHeading, "Usage:");
  help.Write(" ");
  help.Write(nglog::HelpTextStyle::kExecutable, "flags_unittest");
  help.Write(" ");
  help.Write(nglog::HelpTextStyle::kUsageOptions, "[options]");
  help.Write(" ");
  help.Write(nglog::HelpTextStyle::kOptionPrefix, "--");
  help.Write(nglog::HelpTextStyle::kOption, "test_bool_flag");
  help.Write("\nUse ");
  help.Write(nglog::HelpTextStyle::kStream, "stderr");
  help.Write(" and ");
  help.Write(nglog::HelpTextStyle::kConstant, "true");
  help.Write(" or ");
  help.Write(nglog::HelpTextStyle::kConstant, "false");
  help.Write(".\n");
}

void WriteCustomFlagHelp(nglog::HelpWriter& help, const nglog::FlagInfo& flag) {
  help.Write(nglog::HelpTextStyle::kOptionPrefix, "--");
  help.Write(nglog::HelpTextStyle::kOption, flag.name);
  help.Write("=");
  help.Write(nglog::HelpTextStyle::kType, "string[custom]");
  help.Write(" (default=");
  help.Write(nglog::HelpTextStyle::kValue, "<redacted>");
  help.Write(")\n      Custom string help.\n");
}

TEST(Flags, TypedDefinitionsExposeValues) {
  EXPECT_FALSE(nglog::GetFlag(FLAGS_test_bool_flag));
  EXPECT_EQ(7, nglog::GetFlag(FLAGS_test_int32_flag));
  EXPECT_EQ(11U, nglog::GetFlag(FLAGS_test_uint32_flag));
  EXPECT_EQ("default", nglog::GetFlag(FLAGS_test_string_flag));
  EXPECT_EQ(7, nglog::GetFlag(FLAGS_test_custom_flag).value);
}

TEST(Flags, SetFlagAcceptsConstructibleValue) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_test_string_flag, "updated");
  EXPECT_THAT(nglog::GetFlag(FLAGS_test_string_flag),
              testing::StrEq("updated"));
}

TEST(Flags, SetFlagConstructsTheTargetTypeWhenNeeded) {
  nglog::DynamicFlag<ConstructibleOnlyFlagValue> flag(
      "constructible_only_flag", __FILE__, "Constructible-only flag.",
      ConstructibleOnlyFlagValue{7});
  nglog::SetFlag(flag, "9");
  EXPECT_EQ(9, nglog::GetFlag(flag).value);
}

TEST(Flags, SetFlagAcceptsAssignableValue) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_test_custom_flag, "9");
  EXPECT_EQ(9, nglog::GetFlag(FLAGS_test_custom_flag).value);
}

TEST(Flags, ScopedFlagsRestoresAllFlags) {
  const bool original = nglog::GetFlag(FLAGS_test_bool_flag);

  {
    nglog::ScopedFlags<> save;
    nglog::SetFlag(FLAGS_test_bool_flag, !original);
    EXPECT_EQ(!original, nglog::GetFlag(FLAGS_test_bool_flag));
  }

  EXPECT_EQ(original, nglog::GetFlag(FLAGS_test_bool_flag));
}

#ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, ScopedFlagsIgnoreDestroyedFlags) {
  EXPECT_EXIT(
      {
        auto* flag = new nglog::DynamicFlag<std::int32_t>(
            "scoped_dynamic_flag", __FILE__, "Dynamic flag.", 1);
        {
          nglog::ScopedFlags<> save;
          delete flag;
        }
        std::exit(EXIT_SUCCESS);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
}
#endif

TEST(Flags, ScopedFlagsIgnoreReplacementAtTheSameAddress) {
  ReplaceableFlag flag("scoped_all_dynamic_flag", kOriginalValue);
  {
    nglog::ScopedFlags<> save;
    flag.Replace(kReplacementValue);
    EXPECT_EQ(kReplacementValue, nglog::GetFlag(flag.get()));
  }
  EXPECT_EQ(kReplacementValue, nglog::GetFlag(flag.get()));
}

TEST(Flags, ScopedFlagIgnoresDestroyedFlags) {
  ReplaceableFlag flag("scoped_selected_dynamic_flag", kOriginalValue);
  {
    auto save = nglog::MakeScopedFlag(flag.get(), kTemporaryValue);
    flag.Replace(kReplacementValue);
    EXPECT_EQ(kReplacementValue, nglog::GetFlag(flag.get()));
  }
  EXPECT_EQ(kReplacementValue, nglog::GetFlag(flag.get()));
}

TEST(Flags, ScopedFlagsRestoresSelectedFlags) {
  const bool original_bool = nglog::GetFlag(FLAGS_test_bool_flag);
  const std::string original_string = nglog::GetFlag(FLAGS_test_string_flag);

  {
    auto save = nglog::MakeScopedFlags(
        nglog::MakeScopedFlag(FLAGS_test_bool_flag, !original_bool),
        nglog::MakeScopedFlag(FLAGS_test_string_flag, "temporary"));
    EXPECT_EQ(!original_bool, nglog::GetFlag(FLAGS_test_bool_flag));
    EXPECT_THAT(nglog::GetFlag(FLAGS_test_string_flag),
                testing::StrEq("temporary"));
  }

  EXPECT_EQ(original_bool, nglog::GetFlag(FLAGS_test_bool_flag));
  EXPECT_THAT(nglog::GetFlag(FLAGS_test_string_flag),
              testing::StrEq(original_string));
}

#ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, ScopedFlagsRestoreFailureUsesErrorStyling) {
  ASSERT_DEATH(
      {
        RestoreFailureFlag flag;
        nglog::ScopedFlags<> save;
      },
      "\033\\[1;31merror:\033\\[0m"
      ".*could not restore command-line option ");
}
#endif

TEST(Flags, TypedFlagObjectsProvideSynchronizedAccess) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_test_int32_flag, kFirstWrittenValue);
  std::atomic<bool> stop{false};
  std::thread writer([&stop] {
    while (!stop.load(std::memory_order_relaxed)) {
      nglog::SetFlag(FLAGS_test_int32_flag, kFirstWrittenValue);
      nglog::SetFlag(FLAGS_test_int32_flag, kSecondWrittenValue);
    }
  });

  for (int iteration_count = 0; iteration_count < kReadIterations;
       ++iteration_count) {
    EXPECT_THAT(nglog::GetFlag(FLAGS_test_int32_flag),
                testing::AnyOf(kFirstWrittenValue, kSecondWrittenValue));
  }

  stop.store(true, std::memory_order_relaxed);
  writer.join();
}

TEST(Flags, NonTrivialFlagObjectsProvideSynchronizedAccess) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_test_string_flag, std::string("first"));
  std::atomic<bool> stop{false};
  std::thread writer([&stop] {
    while (!stop.load(std::memory_order_relaxed)) {
      nglog::SetFlag(FLAGS_test_string_flag, std::string("first"));
      nglog::SetFlag(FLAGS_test_string_flag, std::string("second"));
    }
  });

  for (int iteration_count = 0; iteration_count < kReadIterations;
       ++iteration_count) {
    EXPECT_THAT(nglog::GetFlag(FLAGS_test_string_flag),
                testing::AnyOf("first", "second"));
  }

  stop.store(true, std::memory_order_relaxed);
  writer.join();
}

TEST(Flags, DefaultValuesCanBeAccessedConcurrently) {
  nglog::DynamicFlag<std::string> flag("concurrent_default_flag", __FILE__,
                                       "Concurrent default flag.", "first");
  std::atomic<bool> stop{false};
  std::thread writer([&flag, &stop] {
    while (!stop.load(std::memory_order_relaxed)) {
      std::string error;
      if (!flag.SetDefaultFromString("first", &error) ||
          !flag.SetDefaultFromString("second", &error)) {
        break;
      }
    }
  });

  for (int iteration_count = 0; iteration_count < kReadIterations;
       ++iteration_count) {
    EXPECT_THAT(flag.DefaultValue(), testing::AnyOf("first", "second"));
  }

  stop.store(true, std::memory_order_relaxed);
  writer.join();
}

TEST(Flags, ParsesTypedValues) {
  nglog::ScopedFlags<> save;
  CommandLine command_line({"--test_bool_flag", "--test_int32_flag=-13",
                            "--test_uint32_flag=19",
                            "--test_string_flag=changed"});

  ASSERT_EQ(4U, command_line.Parse());
  EXPECT_TRUE(nglog::GetFlag(FLAGS_test_bool_flag));
  EXPECT_EQ(-13, nglog::GetFlag(FLAGS_test_int32_flag));
  EXPECT_EQ(19U, nglog::GetFlag(FLAGS_test_uint32_flag));
  EXPECT_EQ("changed", nglog::GetFlag(FLAGS_test_string_flag));
  EXPECT_THAT(command_line.Arguments(), testing::IsEmpty());
}

TEST(Flags, FlagCallbacksMayReadTheRegistry) {
  ReentrantFlag flag;
  std::string error;
  EXPECT_TRUE(nglog::SetCommandLineOption("reentrant_flag", "value", &error));
  EXPECT_THAT(error, testing::IsEmpty());
}

TEST(Flags, ParsesAnEmptyStringValue) {
  nglog::ScopedFlags<> save;
  CommandLine command_line({"--test_string_flag="});

  ASSERT_EQ(1U, command_line.Parse());
  EXPECT_THAT(nglog::GetFlag(FLAGS_test_string_flag), testing::IsEmpty());
}

TEST(Flags, KeepsConsumedValuesWhenRequested) {
  nglog::ScopedFlags<> save;
  CommandLine command_line({"--test_int32_flag", "-13", "positional"});

  ASSERT_EQ(1U, command_line.Parse(nglog::FlagParsingMode::kKeepFlags));
  EXPECT_THAT(command_line.Arguments(),
              testing::ElementsAre("--test_int32_flag", "-13", "positional"));
}

TEST(Flags, ParsesDecimalNumbersWithLeadingZeroes) {
  nglog::ScopedFlags<> save;
  CommandLine command_line({"--test_int32_flag=010", "--test_uint32_flag=08"});

  ASSERT_EQ(2U, command_line.Parse());
  EXPECT_EQ(10, nglog::GetFlag(FLAGS_test_int32_flag));
  EXPECT_EQ(8U, nglog::GetFlag(FLAGS_test_uint32_flag));
}

TEST(Flags, ParsesMixedCaseBooleanValues) {
  nglog::ScopedFlags<> save;
  CommandLine command_line({"--test_bool_flag=TrUe"});

  ASSERT_EQ(1U, command_line.Parse());
  EXPECT_TRUE(nglog::GetFlag(FLAGS_test_bool_flag));
}

TEST(Flags, ExposesTypedReflection) {
  EXPECT_THAT(FLAGS_test_bool_flag.type_name(), testing::StrEq("bool"));
  EXPECT_THAT(FLAGS_test_int32_flag.type_name(), testing::StrEq("int32"));
  EXPECT_THAT(FLAGS_test_uint32_flag.type_name(), testing::StrEq("uint32"));
  EXPECT_THAT(FLAGS_test_string_flag.type_name(), testing::StrEq("string"));
  EXPECT_FALSE(nglog::GetFlag(FLAGS_test_bool_flag));
}

TEST(Flags, ExplicitFlagHelpRetainsPlainReflectionText) {
  EXPECT_THAT(
      AllFlags(),
      testing::Contains(testing::AllOf(
          FlagWithHelp("logtostderr",
                       "Send log messages to stderr instead of log "
                       "files."),
          testing::Field("help_function", &nglog::FlagInfo::help_function,
                         testing::NotNull()))));
}

TEST(Flags, BuiltInHelpUsesUserFriendlyWording) {
  EXPECT_THAT(
      AllFlags(),
      testing::IsSupersetOf({
          FlagWithHelp("alsologtoemail",
                       "Send log messages to each address in this "
                       "comma-separated list in addition to log files."),
          FlagWithHelp("log_file_header",
                       "Write a header at the beginning of each log file."),
          FlagWithHelp("logbufsecs",
                       "Keep buffered log messages for no more than this many "
                       "seconds."),
          FlagWithHelp("logfile_mode",
                       "Set the permissions for newly created log files."),
          FlagWithHelp("max_log_size",
                       "Limit each log file to approximately this size in MiB. "
                       "A value of 0 is treated as 1 and values above 4095 are "
                       "capped at 4095."),
          FlagWithHelp("symbolize_line_info",
                       "Resolve source file names and line numbers in stack "
                       "frames with addr2line or libbacktrace, when "
                       "available."),
          FlagWithHelp("symbolize_file_base_path",
                       "Use this base path to resolve relative source paths "
                       "when creating terminal hyperlinks."),
          FlagWithHelp("symbolize_hyperlinks",
                       "Add hyperlinks to file:line references in colorized "
                       "stack traces when supported by the terminal. Set false "
                       "to disable them."),
      }));
}

TEST(Flags, FlagInfoPreservesExistingAggregateFieldOrder) {
  const nglog::FlagInfo info{"name", "type", "help", "current", "default",
                             "file", {},     {},     {}};
  EXPECT_THAT(info.name, testing::StrEq("name"));
  EXPECT_THAT(info.type, testing::StrEq("type"));
  EXPECT_THAT(info.help, testing::StrEq("help"));
  EXPECT_THAT(info.current_value, testing::StrEq("current"));
  EXPECT_THAT(info.default_value, testing::StrEq("default"));
  EXPECT_THAT(info.filename, testing::StrEq("file"));
  EXPECT_THAT(info.module, testing::IsEmpty());
}

TEST(Flags, HelpUsesTerminalStyling) {
  const std::string output = PrintedHelp();
  EXPECT_THAT(output, testing::HasSubstr("\033["));
  EXPECT_THAT(output, testing::HasSubstr("Environment variables:"));
  EXPECT_THAT(output, testing::HasSubstr("NGLOG_"));
  EXPECT_THAT(output, testing::HasSubstr("\033[37mOptions provided by \033[0m"
                                         "\033[1;37mng-log\033[0m"
                                         "\033[37m:\033[0m\n\n"));
  EXPECT_THAT(output,
              testing::HasSubstr("\033[39m  --\033[0m\033[1;36mhelp\033[0m\n"
                                 "\033[2;39m      Show available command-line "
                                 "options and exit.\033[0m"));
  EXPECT_THAT(output,
              testing::HasSubstr("Command-line options take precedence"));
  EXPECT_THAT(output, testing::HasSubstr("\033[1;32mEnvironment variables:"));
  EXPECT_THAT(output, testing::HasSubstr("\033[34mNGLOG_\033[0m"
                                         "\033[1;36m<flag>\033[0m"
                                         "\033[2;39m=\033[0m"
                                         "\033[35m<value>\033[0m"));
  EXPECT_THAT(output, testing::HasSubstr(
                          "\033[2;39m  Sets \033[0m\033[1;37mng-log\033[0m"
                          "\033[2;39m flag default. Command-line options "
                          "take precedence.\033[0m"));
  EXPECT_THAT(output,
              testing::HasSubstr("\033[2;39mSend log messages to \033[0m"
                                 "\033[34mstderr\033[0m"));
  EXPECT_THAT(output, testing::HasSubstr("\033[39m--\033[0m"
                                         "\033[1;36malsologtostderr\033[0m"));
  EXPECT_THAT(output, testing::HasSubstr("\033[36mfile:line\033[0m"));
  EXPECT_THAT(output, testing::HasSubstr("\033[34maddr2line\033[0m"));
  EXPECT_THAT(output,
              testing::HasSubstr("\033[34maddr2line\033[0m\033[2;39m to "
                                 "resolve"));
  EXPECT_THAT(output, testing::HasSubstr("\033[34mNO_COLOR\033[0m"));
  EXPECT_THAT(output, testing::HasSubstr("\033[34mTERM\033[0m"
                                         "\033[2;39m=\033[0m"
                                         "\033[35mdumb\033[0m"));
  EXPECT_THAT(output, testing::HasSubstr("\033[34mstderr\033[0m"));
  EXPECT_THAT(output, testing::HasSubstr("\033[34mstdout\033[0m"));
  EXPECT_THAT(output,
              testing::HasSubstr("\033[35mfalse\033[0m\033[39m)\033[0m\n"));
  EXPECT_THAT(output,
              testing::HasSubstr("\033[35m\"\"\033[0m\033[39m)\033[0m"));
  EXPECT_THAT(
      output,
      testing::HasSubstr(
          "This help text is long enough to wrap at the default terminal width"
          " for\033[0m\n\033[2;39m      \033[0m\033[2;39mtests."));
  EXPECT_THAT(output, testing::HasSubstr("Test boolean flag."));
  EXPECT_THAT(output, testing::HasSubstr("test_bool_flag"));
}

TEST(Flags, PlainHelpTextIsNotInterpreted) {
  const std::string output = PrintedHelp();
  EXPECT_THAT(
      output,
      testing::HasSubstr(
          "Literal text mentions --not-a-real-option, TERM=dumb, stdout, "
          "and"));
  EXPECT_THAT(output, testing::HasSubstr("file:line."));
}

TEST(Flags, StyledHelpWrapsAcrossStyleBoundaries) {
  const std::string output = PrintedHelp();
  EXPECT_THAT(
      output,
      testing::HasSubstr(
          "\033[2;39m      \033[0m"
          "\033[2;39mThis styled help text is deliberately long so wrapping "
          "must continue\033[0m\n"
          "\033[2;39m      \033[0m"
          "\033[2;39mafter \033[0m"
          "\033[39m--\033[0m"
          "\033[1;36mstyled_help_marker\033[0m"));
  EXPECT_THAT(
      output,
      testing::HasSubstr(
          "styled_help_marker\033[0m\033[2;39m and keep rendering the "
          "remaining explanation\033[0m\n"
          "\033[2;39m      \033[0m\033[2;39mon a separate line.\033[0m"));
}

#ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, MisspelledFlagsSuggestTheClosestRegisteredFlag) {
  CommandLine command_line({"--test_bool_flga"});
  ASSERT_EXIT(command_line.Parse(), testing::ExitedWithCode(EXIT_FAILURE),
              "\033\\[1;31merror:\033\\[0m"
              ".*unknown command-line option");

  ASSERT_EXIT(command_line.Parse(), testing::ExitedWithCode(EXIT_FAILURE),
              "\033\\[2;39m    Did you mean \033\\[0m"
              "\033\\[39m--\033\\[0m\033\\[1;36mtest_bool_fl\033\\[0m"
              "\033\\[1;3;4;36mag\033\\[0m"
              "\033\\[2;39m\\?\033\\[0m");

  ASSERT_EXIT(command_line.Parse(), testing::ExitedWithCode(EXIT_FAILURE),
              "\033\\[39mUse \033\\[0m"
              "\033\\[39m--\033\\[0m\033\\[1;36mhelp\033\\[0m"
              "\033\\[39m to list all available options.\033\\[0m");
}
#endif

TEST(Flags, FuzzyMatchingUsesLevenshteinDistance) {
  EXPECT_EQ(3U, nglog::internal::LevenshteinDistance("kitten", "sitting"));
  const std::vector<std::string> candidates = {"test_bool_flag", "vmodule"};
  EXPECT_THAT(nglog::internal::FindClosestMatch("test_bool_flga", candidates),
              testing::StrEq("test_bool_flag"));
  EXPECT_THAT(nglog::internal::FindClosestMatch("unrelated_option", candidates),
              testing::IsEmpty());
}

#ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, MisspelledHelpSuggestsHelp) {
  CommandLine command_line({"--halp"});
  ASSERT_EXIT(command_line.Parse(), testing::ExitedWithCode(EXIT_FAILURE),
              "\033\\[2;39m    Did you mean \033\\[0m"
              "\033\\[39m--\033\\[0m\033\\[1;36mh\033\\[0m"
              "\033\\[1;3;4;36me\033\\[0m\033\\[1;36mlp\033\\[0m"
              "\033\\[2;39m\\?\033\\[0m");
}
#endif

TEST(Flags, UnknownFlagsCanBeIgnoredExplicitly) {
  CommandLine command_line({"--flag_from_another_library"});
  EXPECT_EQ(0U, command_line.Parse(nglog::FlagParsingMode::kRemoveFlags,
                                   nglog::UnknownFlagMode::kIgnore));
  EXPECT_THAT(command_line.Arguments(),
              testing::ElementsAre("--flag_from_another_library"));
}

TEST(Flags, SeparatorIsRemovedWithTheFlags) {
  nglog::ScopedFlags<> save;
  CommandLine command_line(
      {"--test_bool_flag", "--", "--test_int32_flag=9", "--"});
  EXPECT_EQ(1U, command_line.Parse());
  EXPECT_THAT(command_line.Arguments(),
              testing::ElementsAre("--test_int32_flag=9", "--"));
  EXPECT_EQ(nullptr, command_line.Terminator());
  EXPECT_EQ(7, nglog::GetFlag(FLAGS_test_int32_flag));
}

TEST(Flags, SeparatorIsKeptForAnotherParser) {
  nglog::ScopedFlags<> save;
  CommandLine command_line({"--test_bool_flag", "--flag_from_another_library",
                            "--", "--not_an_option"});
  EXPECT_EQ(1U, command_line.Parse(nglog::FlagParsingMode::kRemoveFlags,
                                   nglog::UnknownFlagMode::kIgnore));
  EXPECT_THAT(command_line.Arguments(),
              testing::ElementsAre("--flag_from_another_library", "--",
                                   "--not_an_option"));
}

#ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, SingleDashOptionsAreRejected) {
  CommandLine command_line({"-test_int32_flag=9"});
  ASSERT_EXIT(command_line.Parse(), testing::ExitedWithCode(EXIT_FAILURE),
              "unknown command-line option \033\\[0m"
              "\033\\[39m-\033\\[0m"
              "\033\\[1;36mtest_int32_flag\033\\[0m");
}

TEST(Flags, SingleDashOptionsSuggestTheLongOptionAsSpelled) {
  CommandLine command_line({"-test-bool-flag"});
  ASSERT_EXIT(command_line.Parse(), testing::ExitedWithCode(EXIT_FAILURE),
              "Did you mean \033\\[0m"
              "\033\\[39m--\033\\[0m"
              "\033\\[1;36mtest-bool-flag\033\\[0m"
              "\033\\[2;39m\\?");
}

TEST(Flags, SingleDashNegationsSuggestTheLongOption) {
  CommandLine command_line({"-notest_bool_flag"});
  ASSERT_EXIT(command_line.Parse(), testing::ExitedWithCode(EXIT_FAILURE),
              "Did you mean \033\\[0m"
              "\033\\[39m--\033\\[0m"
              "\033\\[1;36mnotest_bool_flag\033\\[0m"
              "\033\\[2;39m\\?");
}

TEST(Flags, SingleDashNegationsOfNonBooleanFlagsSuggestTheFlag) {
  CommandLine command_line({"-notest_int32_flag"});
  ASSERT_EXIT(command_line.Parse(), testing::ExitedWithCode(EXIT_FAILURE),
              "Did you mean \033\\[0m"
              "\033\\[39m--\033\\[0m"
              "\033\\[1;36mtest_int32_flag\033\\[0m"
              "\033\\[2;39m\\?");
}

TEST(Flags, SingleDashHelpSuggestsTheLongOption) {
  CommandLine command_line({"-help"});
  ASSERT_EXIT(command_line.Parse(), testing::ExitedWithCode(EXIT_FAILURE),
              "Did you mean \033\\[0m"
              "\033\\[39m--\033\\[0m"
              "\033\\[1;36mhelp\033\\[0m"
              "\033\\[2;39m\\?");
}

TEST(Flags, SingleDashOptionsAreRejectedWhenFlagsAreKept) {
  CommandLine command_line({"-x"});
  ASSERT_EXIT(command_line.Parse(nglog::FlagParsingMode::kKeepFlags),
              testing::ExitedWithCode(EXIT_FAILURE),
              "unknown command-line option \033\\[0m"
              "\033\\[39m-\033\\[0m"
              "\033\\[1;36mx\033\\[0m");
}
#endif

TEST(Flags, SingleDashOptionsAreKeptForAnotherParser) {
  nglog::ScopedFlags<> save;
  CommandLine command_line({"-x", "-test_int32_flag=9"});
  EXPECT_EQ(0U, command_line.Parse(nglog::FlagParsingMode::kRemoveFlags,
                                   nglog::UnknownFlagMode::kIgnore));
  EXPECT_THAT(command_line.Arguments(),
              testing::ElementsAre("-x", "-test_int32_flag=9"));
  EXPECT_EQ(7, nglog::GetFlag(FLAGS_test_int32_flag));
}

TEST(Flags, SingleDashIsAPositionalArgument) {
  CommandLine command_line({"-"});
  EXPECT_EQ(0U, command_line.Parse());
  EXPECT_THAT(command_line.Arguments(), testing::ElementsAre("-"));
}

TEST(Flags, SingleDashArgumentsAfterTheSeparatorArePositional) {
  CommandLine command_line({"--", "-x"});
  EXPECT_EQ(0U, command_line.Parse());
  EXPECT_THAT(command_line.Arguments(), testing::ElementsAre("-x"));
}

TEST(Flags, SeparatorIsKeptWhenFlagsAreKept) {
  nglog::ScopedFlags<> save;
  CommandLine command_line({"--", "file"});
  EXPECT_EQ(0U, command_line.Parse(nglog::FlagParsingMode::kKeepFlags));
  EXPECT_THAT(command_line.Arguments(), testing::ElementsAre("--", "file"));
}

TEST(Flags, OptionsCanBeGrouped) {
  const nglog::FlagModule module("test-options", "Options provided by", "");
  const auto group = module.AddGroup("Test options");
  ASSERT_TRUE(group.AddFlag(FLAGS_test_wrapped_help_flag));

  EXPECT_THAT(AllFlags(),
              testing::Contains(testing::AllOf(
                  FlagNamed("test_wrapped_help_flag"),
                  testing::Field(&nglog::FlagInfo::group, "Test options"))));

  const std::string output = PrintedHelp();
  EXPECT_THAT(output, testing::HasSubstr("\033[1;32mTest options:\033[0m"));
  EXPECT_THAT(output, testing::Not(testing::HasSubstr("Options:")));
  EXPECT_THAT(output, testing::HasSubstr("test_wrapped_help_flag\033[0m"
                                         "\033[2;39m=\033[0m"
                                         "\033[33mstring\033[0m\033[39m"));
  EXPECT_THAT(output, testing::HasSubstr("\n\n\033[1;32mTest options:\033[0m"));
}

TEST(Flags, ModulesOwnMandatoryFlagGroups) {
  const nglog::FlagModule module("mandatory-group-module",
                                 "Options provided by", "");
  const auto group = module.AddGroup("Grouped options");
  ASSERT_TRUE(group.AddFlag(FLAGS_test_custom_flag));

  EXPECT_THAT(
      AllFlags(),
      testing::Contains(testing::AllOf(
          FlagNamed("test_custom_flag"),
          testing::Field(&nglog::FlagInfo::module, "mandatory-group-module"),
          testing::Field(&nglog::FlagInfo::group, "Grouped options"))));
}

TEST(Flags, OptionsCanBeGroupedByModule) {
  const nglog::FlagModule module("test-application", "Options provided by",
                                 "MYAPP_");
  const auto group = module.AddGroup("Test module options");
  ASSERT_TRUE(group.AddFlag(FLAGS_test_bool_flag));
  const nglog::FlagModule foreign_module("foreign-test-application",
                                         "Options provided by", "FOREIGN_");
  const auto foreign_group = foreign_module.AddGroup("Foreign options");
  EXPECT_FALSE(foreign_group.AddFlag(FLAGS_test_bool_flag));
  const nglog::FlagModule second_module("test-library", "Options provided by",
                                        "");
  const auto second_group = second_module.AddGroup("Integer options");
  ASSERT_TRUE(second_group.AddFlag(FLAGS_test_int32_flag));

  EXPECT_THAT(
      AllFlags(),
      testing::Contains(testing::AllOf(
          FlagNamed("test_bool_flag"),
          testing::Field(&nglog::FlagInfo::module, "test-application"))));

  const std::string output = PrintedHelp();
  EXPECT_THAT(output, testing::HasSubstr("\033[37mOptions provided by \033[0m"
                                         "\033[1;37mtest-application\033[0m"
                                         "\033[37m:\033[0m\n\n"));
  EXPECT_THAT(output, testing::HasSubstr(
                          "\033[2;39m        \033[0m"
                          "\033[2;39mTest boolean flag.\033[0m\n\n"
                          "\033[37mOptions provided by \033[0m"
                          "\033[1;37mtest-library\033[0m\033[37m:\033[0m\n\n"));
  EXPECT_THAT(output, testing::Not(testing::HasSubstr(
                          "Options provided by the test application.")));
  EXPECT_THAT(output,
              testing::HasSubstr("\033[1;32mTest module options:\033[0m"));
  EXPECT_THAT(output, testing::HasSubstr("\033[34mMYAPP_\033[0m"
                                         "\033[1;36m<flag>\033[0m"));
  EXPECT_THAT(output, testing::HasSubstr("\033[34mNGLOG_\033[0m"
                                         "\033[1;36m<flag>\033[0m"));
}

#ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, DuplicateModuleNamesAreFatal) {
  ASSERT_DEATH(
      {
        const nglog::FlagModule first("duplicate-module", "Options provided by",
                                      "");
        const nglog::FlagModule second("duplicate-module",
                                       "Options provided by", "");
      },
      "\033\\[1;31merror:\033\\[0m"
      ".*flag module.*duplicate-module.*registered more than once");
}
#endif

TEST(Flags, CustomModuleHeadersArePrinted) {
  const nglog::FlagModule module("custom-header-module", "Application options",
                                 "");
  const auto group = module.AddGroup("Custom options");
  ASSERT_TRUE(group.AddFlag(FLAGS_test_literal_help_flag));

  const std::string output = PrintedHelp();
  EXPECT_THAT(output,
              testing::HasSubstr("\033[37mApplication options \033[0m"
                                 "\033[1;37mcustom-header-module\033[0m"));
}

TEST(Flags, ModuleAssociationFollowsFlagLifetime) {
  const nglog::FlagModule module("dynamic-module", "Options provided by",
                                 "DYNAMIC_");
  const auto group = module.AddGroup("Dynamic options");
  {
    nglog::DynamicFlag<std::int32_t> flag("dynamic_module_flag", __FILE__,
                                          "Dynamic module flag.", 7);
    ASSERT_TRUE(group.AddFlag(flag));
  }

  nglog::DynamicFlag<std::int32_t> replacement("dynamic_module_flag", __FILE__,
                                               "Dynamic module flag.", 11);
  EXPECT_TRUE(group.AddFlag(replacement));
}

TEST(Flags, ModuleEnvironmentPrefixSetsTheFlagDefault) {
  const ScopedEnvironmentVariable variable("DYNAMIC_default_flag", "23");
  const nglog::FlagModule module("environment-module", "Options provided by",
                                 "DYNAMIC_");
  const auto group = module.AddGroup("Environment options");
  {
    nglog::DynamicFlag<std::int32_t> flag("default_flag", __FILE__,
                                          "Environment default flag.", 7);
    ASSERT_TRUE(group.AddFlag(flag));
    EXPECT_EQ(23, nglog::GetFlag(flag));
    EXPECT_THAT(AllFlags(),
                testing::Contains(testing::AllOf(
                    FlagNamed("default_flag"),
                    testing::Field(&nglog::FlagInfo::default_value, "23"))));
  }
}

#ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, HelpNameIsReserved) {
  EXPECT_DEATH(
      {
        auto* flag = new nglog::DynamicFlag<bool>(
            "help", __FILE__, "Application help flag.", false);
        (void)flag;
      },
      "reserved");
}
#endif

TEST(Flags, HelpUsesConsistentTextAndThemeStyles) {
  EXPECT_THAT(AllFlags(), testing::Each(testing::Field(
                              "help", &nglog::FlagInfo::help,
                              testing::AllOf(StartsWithAnUppercaseLetter(),
                                             testing::EndsWith(".")))));

  const std::string output = PrintedHelp();
  EXPECT_THAT(output,
              testing::HasSubstr("\033[1;32mEnvironment variables:\033[0m"));
  EXPECT_THAT(output, testing::HasSubstr("\033[1;36mtest_bool_flag\033[0m"
                                         "\033[2;39m=\033[0m"));
  EXPECT_THAT(output, testing::HasSubstr("\033[2;39m=\033[0m\033[33mbool\033[0m"
                                         "\033[39m (default=\033[0m"));
  EXPECT_THAT(output,
              testing::HasSubstr("\033[35mfalse\033[0m\033[39m)\033[0m"));
  EXPECT_THAT(output, testing::HasSubstr(
                          "\033[39m    --\033[0m"
                          "\033[1;36mvmodule\033[0m"
                          "\033[2;39m=\033[0m\033[33mstring[,string,...]"));
  EXPECT_THAT(output, testing::HasSubstr(
                          "\033[35m<module\033[0m\n"
                          "\033[2;39m        \033[0m\033[35mname>=<log level>"
                          "\033[0m"));
  EXPECT_THAT(output, testing::HasSubstr(
                          "\033[39m    --\033[0m"
                          "\033[1;36mlogfile_mode\033[0m\033[2;39m=\033[0m"
                          "\033[33mmode\033[0m"
                          "\033[39m (default=\033[0m"
                          "\033[35m0664\033[0m\033[39m)\033[0m"));
  EXPECT_THAT(output,
              testing::HasSubstr("\033[39m    --\033[0m"
                                 "\033[1;36mlogbufsecs\033[0m\033[2;39m=\033[0m"
                                 "\033[33mint32\033[0m\033[39m (default=\033[0m"
                                 "\033[35m30 s\033[0m\033[39m)\033[0m"));
  EXPECT_THAT(output,
              testing::HasSubstr(
                  "\033[39m    --\033[0m"
                  "\033[1;36maddr2line_timeout_ms\033[0m\033[2;39m=\033[0m"
                  "\033[33mint32\033[0m\033[39m (default=\033[0m"
                  "\033[35m2000 ms\033[0m\033[39m)\033[0m"));
  EXPECT_THAT(output,
              testing::HasSubstr(
                  "Limit each log file to approximately this size in MiB."));
  EXPECT_THAT(output, testing::HasSubstr("\033[34mNGLOG_\033[0m"
                                         "\033[1;36m<flag>\033[0m"
                                         "\033[2;39m=\033[0m"
                                         "\033[35m<value>\033[0m"));
  EXPECT_THAT(output,
              testing::HasSubstr("\033[2;39mSend log messages to \033[0m"
                                 "\033[34mstderr\033[0m"));
}

TEST(Flags, CustomFlagHelpFormatterOverridesRepresentation) {
  nglog::SetFlagHelpFormatter("test_string_flag", WriteCustomFlagHelp);

  const std::string output = PrintedHelp();
  EXPECT_THAT(output, testing::HasSubstr("\033[39m--\033[0m"
                                         "\033[1;36mtest_string_flag\033[0m"
                                         "\033[2;39m=\033[0m"
                                         "\033[33mstring[custom]\033[0m"
                                         "\033[2;39m (default=\033[0m"
                                         "\033[35m<redacted>\033[0m"
                                         "\033[2;39m)\033[0m"));
  EXPECT_THAT(output, testing::HasSubstr("Custom string help."));

  nglog::SetFlagHelpFormatter("test_string_flag", {});
}

TEST(Flags, GeneratedUsageCanUseTextStyles) {
  nglog::SetUsageMessage(WriteGeneratedUsage);
  EXPECT_EQ(
      "Usage: flags_unittest [options] --test_bool_flag\n"
      "Use stderr and true or false.\n",
      nglog::ProgramUsage());

  const std::string output = PrintedHelp();
  EXPECT_THAT(output, testing::HasSubstr("\033[1;32mUsage:\033[0m"
                                         "\033[2;39m \033[0m"
                                         "\033[34mflags_unittest\033[0m"
                                         "\033[2;39m \033[0m"
                                         "\033[39m[options]\033[0m"
                                         "\033[2;39m \033[0m"
                                         "\033[39m--\033[0m"
                                         "\033[1;36mtest_bool_flag\033[0m\n"));
  EXPECT_THAT(output, testing::HasSubstr("\033[2;39mUse \033[0m"
                                         "\033[34mstderr\033[0m"
                                         "\033[2;39m and \033[0m"
                                         "\033[35mtrue\033[0m"
                                         "\033[2;39m or \033[0m"
                                         "\033[35mfalse\033[0m"
                                         "\033[2;39m.\033[0m\n"));

  nglog::SetUsageMessage("Plain usage.");
  EXPECT_THAT(PrintedHelp(),
              testing::HasSubstr("\033[2;39mPlain usage.\033[0m\n"));

  nglog::SetUsageMessage(std::string());
}

#ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, UsageCallbacksMayUseFlagRegistry) {
  ASSERT_EXIT(
      {
        nglog::SetUsageMessage([](nglog::HelpWriter& help) {
          std::vector<nglog::FlagInfo> flags;
          nglog::GetAllFlags(&flags);
          help.Write("Usage callback completed.");
        });
        // A deadlock never resolves, so the generous deadline only
        // separates it from a slow but correct run.
        constexpr std::chrono::seconds kDeadlockTimeout{30};
        std::promise<void> completed;
        std::future<void> completion = completed.get_future();
        std::thread usage_thread([&completed] {
          nglog::ProgramUsage();
          completed.set_value();
        });
        if (completion.wait_for(kDeadlockTimeout) !=
            std::future_status::ready) {
          std::exit(EXIT_FAILURE);
        }
        usage_thread.join();
        std::exit(EXIT_SUCCESS);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
  nglog::SetUsageMessage(std::string());
}
#endif

TEST(Flags, InvalidTypedValuePreservesCurrentValue) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_test_int32_flag, 23);
  std::string error;
  EXPECT_FALSE(
      nglog::SetCommandLineOption("test_int32_flag", "not-an-int", &error));
  EXPECT_THAT(error, testing::HasSubstr("not-an-int"));
  EXPECT_EQ(23, nglog::GetFlag(FLAGS_test_int32_flag));
}

#ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, InvalidCommandLineValueExitsWithFailureAndUsesTerminalStyling) {
  CommandLine command_line({"--test_uint32_flag=-1"});
  ASSERT_EXIT(command_line.Parse(), testing::ExitedWithCode(EXIT_FAILURE),
              "\033\\[1;31merror:\033\\[0m"
              ".*\033\\[39m--\033\\[0m"
              "\033\\[1;36mtest_uint32_flag\033\\[0m.*"
              "\033\\[35m-1\033\\[0m.*"
              "\033\\[33muint32\033\\[0m.*"
              "\033\\[35m0\033\\[0m.*"
              "\033\\[35m4294967295\033\\[0m.*");
}
#endif

#ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, DuplicateDefinitionsAreFatal) {
  ASSERT_DEATH(
      {
        const nglog::DynamicFlag<bool> first("duplicate_flag_test", __FILE__,
                                             "", false);
        const nglog::DynamicFlag<bool> second("duplicate_flag_test", __FILE__,
                                              "", false);
      },
      "\033\\[1;31merror:\033\\[0m"
      ".*command-line option.*duplicate_flag_test.*defined more than once"
      // Every line is matched separately because the simple regular
      // expressions on Windows do not match line breaks with a period.
      ".*\n.*First:.*\n.*Second:"
      ".*\n.*Reason:.*The same source file is compiled into more than one"
      ".*\n.*or executable of this program\\."
      ".*\n.*Hint:.*Compile the file into only one of them");
}
#endif

TEST(Flags, ProcessFlagRegistryReportsDuplicatesAcrossLibraryCopies) {
  nglog::internal::ProcessFlagRegistry first;
  nglog::internal::ProcessFlagRegistry second;
  nglog::internal::ProcessFlagInfo previous;
  const char name[] = "process_registry_test_flag";
  ASSERT_EQ(nglog::internal::ProcessFlagRegistration::kRegistered,
            first.Register(name, "first/flags.cc", "int32", &previous));
  EXPECT_EQ(nglog::internal::ProcessFlagRegistration::kDuplicate,
            second.Register(name, "second/flags.cc", "bool", &previous));
  EXPECT_THAT(previous.filename, testing::StrEq("first/flags.cc"));
  EXPECT_THAT(previous.type, testing::StrEq("int32"));
  second.Unregister(name, "second/flags.cc");
  first.Unregister(name, "first/flags.cc");
}

#ifdef HAVE_FORK
#  ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, ProcessFlagRegistryIsolatesForkedChildren) {
  nglog::internal::ProcessFlagRegistry parent;
  nglog::internal::ProcessFlagInfo previous;
  ASSERT_EQ(nglog::internal::ProcessFlagRegistration::kRegistered,
            parent.Register("fork_parent_flag", "parent/flags.cc", "bool",
                            &previous));
  ASSERT_EXIT(
      {
        nglog::internal::ProcessFlagRegistry child;
        nglog::internal::ProcessFlagInfo child_previous;
        const bool inherited =
            child.Register("fork_parent_flag", "child/flags.cc", "string",
                           &child_previous) ==
            nglog::internal::ProcessFlagRegistration::kDuplicate;
        const bool registered =
            child.Register("fork_child_flag", "child/flags.cc", "string",
                           &child_previous) ==
            nglog::internal::ProcessFlagRegistration::kRegistered;
        std::exit(inherited && registered ? EXIT_SUCCESS : EXIT_FAILURE);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
  EXPECT_EQ(nglog::internal::ProcessFlagRegistration::kRegistered,
            parent.Register("fork_child_flag", "parent/flags.cc", "string",
                            &previous));
  parent.Unregister("fork_child_flag", "parent/flags.cc");
  parent.Unregister("fork_parent_flag", "parent/flags.cc");
}
#  endif
#endif

#ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, DuplicateBuiltInDefinitionsReportBothSourceLocations) {
  ASSERT_DEATH(
      {
        const nglog::DynamicFlag<std::int32_t> duplicate(
            "stderrthreshold", "/path/third-party/application/flags.cc",
            "Duplicate stderr threshold flag.", 2);
      },
      "\033\\[1;31merror:\033\\[0m"
      ".*command-line option.*stderrthreshold.*defined more than once"
      // The path separator in __FILE__ differs between compilers.
      ".*\n.*First:.*src.flags\\.cc"
      ".*\n.*Second:.*third-party/application/flags\\.cc"
      ".*\n.*Reason:.*Two definitions in this program use the same option"
      ".*\n.*Hint:.*Rename one of the options");
}
#endif

TEST(Flags, DestroyedFlagsAreUnregistered) {
  {
    const nglog::DynamicFlag<bool> flag("temporary_flag", __FILE__, "", false);
  }
  const nglog::DynamicFlag<bool> flag("temporary_flag", __FILE__, "", false);
}

#ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, RejectsNegationOfNonBooleanFlags) {
  CommandLine command_line({"--notest_int32_flag"});
  ASSERT_EXIT(command_line.Parse(), testing::ExitedWithCode(EXIT_FAILURE),
              "cannot be negated because it is not boolean");
}
#endif

#ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, RejectsValuesForNegatedBooleanFlags) {
  CommandLine command_line({"--notest_bool_flag=true"});
  ASSERT_EXIT(command_line.Parse(), testing::ExitedWithCode(EXIT_FAILURE),
              "does not accept a value when negated");
}
#endif

#ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, RejectsValuesForHelpOption) {
  CommandLine command_line({"--help=unexpected"});
  ASSERT_EXIT(command_line.Parse(), testing::ExitedWithCode(EXIT_FAILURE),
              "command-line option --help does not accept a value");
}
#endif

#ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, HelpOptionPrintsAndExits) {
  CommandLine command_line({"--help"});
  ASSERT_EXIT(command_line.Parse(), testing::ExitedWithCode(EXIT_SUCCESS),
              "\033\\[39m  --\033\\[0m\033\\[1;36mhelp\033\\[0m\n"
              "\033\\[2;39m      Show available command-line options "
              "and exit.\033\\[0m");
}
#endif

TEST(Flags, UnbrokenHelpWordIsPrintedOnce) {
  const std::string output = StripTerminalStyles(PrintedHelp());
  constexpr char kParagraph[] = "Unbroken_help_second_paragraph.";
  EXPECT_THAT(output,
              testing::HasSubstr("\n      Unbroken_help_second_paragraph.\n"));
  // The first and the last occurrence coincide only for a single one.
  EXPECT_EQ(output.find(kParagraph), output.rfind(kParagraph));
}

#ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, DeclarationWithADifferentTypeThanTheDefinitionIsFatal) {
  ASSERT_DEATH(
      {
        const nglog::DynamicFlag<bool> flag("declared_type_flag",
                                            "app/options.cc", "", false);
        const nglog::internal::FlagDeclaration declaration(
            "declared_type_flag", "int32", "app/options.h");
      },
      "\033\\[1;31merror:\033\\[0m"
      ".*command-line option.*declared_type_flag.*declared with a different "
      "type"
      // Every line is matched separately because the simple regular
      // expressions on Windows do not match line breaks with a period.
      ".*\n.*Declared:.*app/options\\.h.*int32"
      ".*\n.*Defined:.*app/options\\.cc.*bool"
      ".*\n.*Reason:.*"
      ".*\n.*"
      ".*\n.*Hint:.*Use the same type in NGLOG_DECLARE and NGLOG_DEFINE");
}

TEST(Flags, DefinitionWithADifferentTypeThanAnEarlierDeclarationIsFatal) {
  ASSERT_DEATH(
      {
        const nglog::internal::FlagDeclaration declaration(
            "early_declared_type_flag", "int32", "app/options.h");
        const nglog::DynamicFlag<bool> flag("early_declared_type_flag",
                                            "app/options.cc", "", false);
      },
      "declared with a different type");
}
#endif

#ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, RegisteringAFlagMultipleTimesIsFatal) {
  ASSERT_DEATH(
      {
        nglog::DynamicFlag<bool> flag("multiply_registered_flag",
                                      "app/options.cc", "", false);
        nglog::RegisterFlag(flag);
      },
      "\033\\[1;31merror:\033\\[0m"
      ".*command-line option.*multiply_registered_flag.*registered more than "
      "once"
      // Every line is matched separately because the simple regular
      // expressions on Windows do not match line breaks with a period.
      ".*\n.*Defined:.*app/options\\.cc.*bool"
      ".*\n.*Reason:.*The same flag object was registered multiple times"
      ".*\n.*"
      ".*\n.*"
      ".*\n.*"
      ".*\n.*Hint:.*Link each flag definition into exactly one library");
}
#endif

#ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, InvalidNamesAreFatal) {
  for (const char* const name :
       {"", "with space", "key=value", "1st", "dash-ed"}) {
    SCOPED_TRACE(name);
    EXPECT_DEATH(
        {
          const nglog::DynamicFlag<bool> flag(name, "app/options.cc", "",
                                              false);
        },
        "is not a valid command-line option name");
  }
}

TEST(Flags, NegatedNameOfABooleanFlagIsReserved) {
  EXPECT_DEATH(
      {
        const nglog::DynamicFlag<bool> flag("feature", "app/options.cc", "",
                                            true);
        const nglog::DynamicFlag<bool> negation("nofeature", "app/other.cc", "",
                                                false);
      },
      "nofeature.*conflicts with the negation of.*feature");
  EXPECT_DEATH(
      {
        const nglog::DynamicFlag<std::int32_t> negation("noeffect",
                                                        "app/other.cc", "", 0);
        const nglog::DynamicFlag<bool> flag("effect", "app/options.cc", "",
                                            true);
      },
      "noeffect.*conflicts with the negation of.*effect");
}
#endif

TEST(Flags, NonBooleanFlagsMayStartWithNo) {
  const nglog::DynamicFlag<std::int32_t> flag("node_count", __FILE__,
                                              "Node count.", 1);
  const nglog::DynamicFlag<std::int32_t> other("de_count", __FILE__,
                                               "Other count.", 2);
  EXPECT_EQ(1, nglog::GetFlag(flag));
}

TEST(Flags, DeclarationsWithTheDefinitionTypeAreAccepted) {
  const nglog::internal::FlagDeclaration early(
      "matching_declared_flag", nglog::FlagTraits<bool>::TypeName(),
      "app/options.h");
  const nglog::DynamicFlag<bool> flag("matching_declared_flag",
                                      "app/options.cc", "", true);
  const nglog::internal::FlagDeclaration late(
      "matching_declared_flag", nglog::FlagTraits<bool>::TypeName(),
      "app/other.h");
  EXPECT_TRUE(nglog::GetFlag(flag));
}

TEST(Flags, EnvironmentDefaultsKeepExplicitlySetValues) {
  const ScopedEnvironmentVariable variable("EXPLICIT_explicit_flag", "23");
  const nglog::FlagModule module("explicit-module", "Options provided by",
                                 "EXPLICIT_");
  {
    nglog::DynamicFlag<std::int32_t> flag("explicit_flag", __FILE__,
                                          "Explicit flag.", 7);
    nglog::SetFlag(flag, 11);
    ASSERT_TRUE(module.AddGroup("Explicit options").AddFlag(flag));
    EXPECT_EQ(11, nglog::GetFlag(flag));
    EXPECT_THAT(flag.DefaultValue(), testing::StrEq("23"));
  }
}

TEST(Flags, DefaultsAreDeterminedWhenTheFlagIsRegistered) {
  const ScopedEnvironmentVariable variable(kDefaultSourceVariable, "changed");
  EXPECT_THAT(nglog::GetFlag(FLAGS_test_environment_default_flag),
              testing::StrEq("unset"));
}

TEST(Flags, FailedFirstUseLeavesTheFlagUsable) {
  static nglog::Flag<FragileValue> flag(nglog::internal::kStaticFlag,
                                        "fragile_flag", __FILE__, "",
                                        &MakeFragileValue);
  // The first use copies the computed default into the current and then into
  // the default value. The second copy fails.
  g_fragile_copies = 0;
  g_failing_copy = 2;
  EXPECT_THROW(nglog::GetFlag(flag), std::runtime_error);
  g_failing_copy = 0;
  EXPECT_EQ(7, nglog::GetFlag(flag).value);
  EXPECT_THAT(flag.DefaultValue(), testing::StrEq("7"));
}

TEST(Flags, RestoringAValueKeepsItUntouched) {
  const ScopedEnvironmentVariable single_variable(
      "RESTORE_restored_single_flag", "23");
  const ScopedEnvironmentVariable all_variable("RESTORE_restored_all_flag",
                                               "29");
  const nglog::FlagModule module("restore-module", "Options provided by",
                                 "RESTORE_");
  const auto group = module.AddGroup("Restored options");
  nglog::DynamicFlag<std::int32_t> single("restored_single_flag", __FILE__,
                                          "Restored flag.", 7);
  nglog::DynamicFlag<std::int32_t> all("restored_all_flag", __FILE__,
                                       "Restored flag.", 7);
  {
    auto save = nglog::MakeScopedFlag(single, 11);
  }
  {
    nglog::ScopedFlags<> save;
    nglog::SetFlag(all, 13);
  }
  ASSERT_TRUE(group.AddFlag(single));
  ASSERT_TRUE(group.AddFlag(all));
  EXPECT_EQ(23, nglog::GetFlag(single));
  EXPECT_EQ(29, nglog::GetFlag(all));
}

TEST(Flags, IntegersRejectSurroundingWhitespace) {
  constexpr std::int32_t kSignedSentinel = 7;
  constexpr std::uint32_t kUnsignedSentinel = 11;
  for (const char* const value : {" 5", "\t5", "5 ", " 0x5"}) {
    SCOPED_TRACE(value);
    std::int32_t signed_value = kSignedSentinel;
    std::uint32_t unsigned_value = kUnsignedSentinel;
    std::string error;
    EXPECT_FALSE(
        nglog::FlagTraits<std::int32_t>::Parse(value, &signed_value, &error));
    EXPECT_EQ(kSignedSentinel, signed_value);
    EXPECT_FALSE(nglog::FlagTraits<std::uint32_t>::Parse(value, &unsigned_value,
                                                         &error));
    EXPECT_EQ(kUnsignedSentinel, unsigned_value);
  }
  std::uint32_t unsigned_value = kUnsignedSentinel;
  std::string error;
  EXPECT_FALSE(
      nglog::FlagTraits<std::uint32_t>::Parse(" -1", &unsigned_value, &error));
}

TEST(Flags, FileModesUseOctalNotation) {
  nglog::FileMode mode;
  std::string error;
  ASSERT_TRUE(nglog::FlagTraits<nglog::FileMode>::Parse("0600", &mode, &error));
  EXPECT_EQ(0600U, mode.value());
  ASSERT_TRUE(nglog::FlagTraits<nglog::FileMode>::Parse("644", &mode, &error));
  EXPECT_EQ(0644U, mode.value());
  ASSERT_TRUE(
      nglog::FlagTraits<nglog::FileMode>::Parse("07777", &mode, &error));
  EXPECT_EQ(07777U, mode.value());
  EXPECT_THAT(nglog::FlagTraits<nglog::FileMode>::ToString(nglog::FileMode{0}),
              testing::StrEq("0000"));
  EXPECT_THAT(
      nglog::FlagTraits<nglog::FileMode>::ToString(nglog::FileMode{0664}),
      testing::StrEq("0664"));
  EXPECT_THAT(
      nglog::FlagTraits<nglog::FileMode>::ToString(nglog::FileMode{07777}),
      testing::StrEq("07777"));
}

TEST(Flags, FileModesRejectInvalidValues) {
  for (const char* const value :
       {"", "8", "0x1A", "-600", "+600", " 600", "600 ", "010000", "06a4"}) {
    SCOPED_TRACE(value);
    nglog::FileMode mode{0664};
    std::string error;
    nglog::internal::FlagParseError details;
    EXPECT_FALSE(nglog::FlagTraits<nglog::FileMode>::ParseWithDetails(
        value, &mode, &error, &details));
    EXPECT_EQ(0664U, mode.value());
    EXPECT_THAT(details.minimum, testing::StrEq("0000"));
    EXPECT_THAT(details.maximum, testing::StrEq("07777"));
  }
}

TEST(Flags, LogFileModeIsAFileMode) {
  static_assert(std::is_same<decltype(FLAGS_logfile_mode),
                             nglog::Flag<nglog::FileMode>>::value,
                "logfile_mode must be typed as a file mode");
  nglog::ScopedFlags<> save;
  ASSERT_TRUE(nglog::SetCommandLineOption("logfile_mode", "0600"));
  EXPECT_EQ(0600U, nglog::GetFlag(FLAGS_logfile_mode).value());
}

TEST(Flags, StringFlagsTrackEmptinessWithoutLocking) {
  nglog::DynamicFlag<std::string> flag("emptiness_flag", __FILE__,
                                       "Emptiness flag.", "");
  EXPECT_TRUE(nglog::internal::FlagValueAccess::Empty(flag));
  nglog::SetFlag(flag, "value");
  EXPECT_FALSE(nglog::internal::FlagValueAccess::Empty(flag));
  std::string error;
  ASSERT_TRUE(flag.SetFromString("", &error));
  EXPECT_TRUE(nglog::internal::FlagValueAccess::Empty(flag));

  // A new default replaces only a value that was not set explicitly.
  nglog::DynamicFlag<std::string> unset("emptiness_default_flag", __FILE__,
                                        "Emptiness default flag.", "");
  ASSERT_TRUE(unset.SetDefaultFromString("default", &error));
  EXPECT_FALSE(nglog::internal::FlagValueAccess::Empty(unset));
}

TEST(Flags, ScopedFlagsRestoreTheOriginalValueOfARepeatedFlag) {
  const std::int32_t original = nglog::GetFlag(FLAGS_test_int32_flag);
  {
    auto save = nglog::MakeScopedFlags(
        nglog::MakeScopedFlag(FLAGS_test_int32_flag, original + 1),
        nglog::MakeScopedFlag(FLAGS_test_int32_flag, original + 2));
    EXPECT_EQ(original + 2, nglog::GetFlag(FLAGS_test_int32_flag));
  }
  EXPECT_EQ(original, nglog::GetFlag(FLAGS_test_int32_flag));
}

TEST(Flags, DisplayedLogFileModeDefaultRestoresTheDefault) {
  nglog::ScopedFlags<> save;
  const std::string output = StripTerminalStyles(PrintedHelp());
  std::smatch match;
  const std::regex entry("--logfile_mode=\\S+ \\(default=([^)]*)\\)");
  ASSERT_TRUE(std::regex_search(output, match, entry)) << output;
  const std::string displayed = match[1].str();

  ASSERT_TRUE(nglog::SetCommandLineOption("logfile_mode", displayed.c_str()));
  EXPECT_THAT(AllFlags(),
              testing::Contains(testing::AllOf(FlagNamed("logfile_mode"),
                                               CurrentValueIsTheDefault())))
      << "displayed default: " << displayed;
}

#ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, ModuleNameCanBeReusedAfterTheModuleIsDestroyed) {
  EXPECT_EXIT(
      {
        {
          const nglog::FlagModule first("reusable-module",
                                        "Options provided by", "");
        }
        const nglog::FlagModule second("reusable-module", "Options provided by",
                                       "");
        std::exit(EXIT_SUCCESS);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
}
#endif

TEST(Flags, DestroyedModuleReleasesItsFlags) {
  nglog::DynamicFlag<std::int32_t> flag("module_lifetime_flag", __FILE__,
                                        "Module lifetime flag.", 1);
  {
    const nglog::FlagModule module("transient-module", "Options provided by",
                                   "");
    ASSERT_TRUE(module.AddGroup("Transient options").AddFlag(flag));
  }

  EXPECT_THAT(
      AllFlags(),
      testing::Contains(testing::AllOf(
          FlagNamed("module_lifetime_flag"),
          testing::Field(&nglog::FlagInfo::module, testing::IsEmpty()),
          testing::Field(&nglog::FlagInfo::group, testing::IsEmpty()))));

  const nglog::FlagModule replacement("replacement-module",
                                      "Options provided by", "");
  EXPECT_TRUE(replacement.AddGroup("Replacement options").AddFlag(flag));
}

TEST(Flags, FlagIsUnregisteredBeforeItsValueIsDestroyed) {
  g_destruction_entered = false;
  g_destruction_released = false;
  auto* const flag = new nglog::DynamicFlag<BlockingDestructionValue>(
      "blocking_destruction_flag", __FILE__, "Blocking destruction flag.",
      BlockingDestructionValue{});
  g_block_next_destruction = true;
  std::thread destroyer([flag] { delete flag; });
  while (!g_destruction_entered) {
    std::this_thread::yield();
  }
  std::string value;
  const bool registered =
      nglog::GetCommandLineOption("blocking_destruction_flag", &value);
  g_destruction_released = true;
  destroyer.join();
  EXPECT_FALSE(registered);
}

#if !defined(NGLOG_OS_WINDOWS)
// Lists the environment variables of the process in their order.
std::vector<std::string> EnvironmentVariables() {
  std::vector<std::string> variables;
  for (char** variable = environ; *variable != nullptr; ++variable) {
    variables.emplace_back(*variable);
  }
  return variables;
}

TEST(Flags, ProcessFlagRegistryDoesNotUseTheEnvironment) {
  const std::vector<std::string> before = EnvironmentVariables();
  {
    nglog::internal::ProcessFlagRegistry registry;
    nglog::internal::ProcessFlagInfo previous;
    ASSERT_EQ(nglog::internal::ProcessFlagRegistration::kRegistered,
              registry.Register("environment_free_flag", "flags.cc", "bool",
                                &previous));
    registry.Unregister("environment_free_flag", "flags.cc");
  }
  EXPECT_EQ(before, EnvironmentVariables());
}
#endif

// Emscripten provides fork, which always fails.
#if defined(HAVE_FORK) && !defined(NGLOG_OS_EMSCRIPTEN)
// A deadlocked child never finishes, so the generous timeout only separates it
// from a child that a loaded machine delays.
constexpr int kChildTimeoutMilliseconds = 30000;
constexpr int kChildPollMilliseconds = 10;

// Waits for a forked child and reports whether it exited successfully before
// the timeout. A child that does not finish in time is killed.
bool ChildSucceeded(pid_t child,
                    int timeout_milliseconds = kChildTimeoutMilliseconds) {
  for (int waited = 0; waited < timeout_milliseconds;
       waited += kChildPollMilliseconds) {
    int status = 0;
    if (waitpid(child, &status, WNOHANG) == child) {
      return WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS;
    }
    std::this_thread::sleep_for(
        std::chrono::milliseconds(kChildPollMilliseconds));
  }
  kill(child, SIGKILL);
  waitpid(child, nullptr, 0);
  return false;
}

// The helper threads are detached and signal their completion through an
// atomic. A forked child would otherwise inherit a finished thread that was
// never joined, which ThreadSanitizer reports as a leak in the child.
// Set by the first call of the default value function, which alone blocks.
std::atomic_flag g_registry_reading_default_consumed = ATOMIC_FLAG_INIT;
std::atomic<bool> g_registry_reading_default_entered{false};
std::atomic<bool> g_registry_reading_default_released{false};
std::atomic<bool> g_registry_reading_default_finished{false};

// The first call blocks until released and then reads the registry while the
// formatting of a flag value holds the registry lock. Later calls, such as
// those in a forked child, read the registry right away.
std::int32_t RegistryReadingDefault() {
  if (!g_registry_reading_default_consumed.test_and_set()) {
    g_registry_reading_default_entered = true;
    while (!g_registry_reading_default_released) {
      std::this_thread::yield();
    }
    std::string value;
    nglog::GetCommandLineOption("slowly_formatted_flag", &value);
    g_registry_reading_default_finished = true;
    return 1;
  }
  std::string value;
  nglog::GetCommandLineOption("v", &value);
  return 1;
}

TEST(Flags, ForkWhileADefaultValueReadsTheRegistryDoesNotDeadlock) {
  // The scenario waits for two children in turn. It gets enough time to kill
  // either of them itself, so that no deadlocked child outlives the test.
  constexpr int kScenarioChildren = 2;
  constexpr int kScenarioTimeoutMilliseconds =
      (kScenarioChildren + 1) * kChildTimeoutMilliseconds;
  // The scenario runs in a child, so that a deadlock cannot stall the test.
  const pid_t scenario = fork();
  if (scenario == 0) {
    const nglog::DynamicFlag<SlowlyFormattedValue> slow(
        "slowly_formatted_flag", __FILE__, "", SlowlyFormattedValue{});
    static nglog::Flag<std::int32_t> flag(
        nglog::internal::kStaticFlag, "registry_reading_default_flag", __FILE__,
        "", &RegistryReadingDefault);
    std::thread([] { nglog::RegisterFlag(flag); }).detach();
    while (!g_registry_reading_default_entered) {
      std::this_thread::yield();
    }
    // The other thread is inside the default value function and holds no
    // lock, so the child can compute the default value itself.
    const pid_t computing_child = fork();
    if (computing_child == 0) {
      _exit(nglog::GetFlag(flag) == 1 ? EXIT_SUCCESS : EXIT_FAILURE);
    }
    const bool computing_child_succeeded = ChildSucceeded(computing_child);
    g_slow_formatting_entered = false;
    g_registry_reading_default_released = true;
    while (!g_slow_formatting_entered) {
      std::this_thread::yield();
    }
    // The default value function now holds the registry lock while it reads
    // the registry. The child must not inherit the lock.
    const pid_t reading_child = fork();
    if (reading_child == 0) {
      std::string value;
      _exit(nglog::GetCommandLineOption("v", &value) ? EXIT_SUCCESS
                                                     : EXIT_FAILURE);
    }
    const bool reading_child_succeeded = ChildSucceeded(reading_child);
    while (!g_registry_reading_default_finished) {
      std::this_thread::yield();
    }
    _exit(computing_child_succeeded && reading_child_succeeded ? EXIT_SUCCESS
                                                               : EXIT_FAILURE);
  }
  ASSERT_NE(-1, scenario);
  EXPECT_TRUE(ChildSucceeded(scenario, kScenarioTimeoutMilliseconds));
}

TEST(Flags, ForkWhileAnotherThreadHoldsTheRegistryLockDoesNotDeadlock) {
  static std::atomic<bool> locked{false};
  static std::atomic<bool> finished{false};
  locked = false;
  finished = false;
  std::thread([] {
    {
      const nglog::internal::RegisteredFlagLock lock(nullptr, 0);
      locked = true;
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    finished = true;
  }).detach();
  while (!locked) {
    std::this_thread::yield();
  }
  const pid_t child = fork();
  if (child == 0) {
    const nglog::DynamicFlag<bool> flag("fork_registry_flag", __FILE__, "",
                                        false);
    _exit(EXIT_SUCCESS);
  }
  while (!finished) {
    std::this_thread::yield();
  }
  ASSERT_NE(-1, child);
  EXPECT_TRUE(ChildSucceeded(child));
}

TEST(Flags, ForkWhileAnotherThreadHoldsAFlagValueLockDoesNotDeadlock) {
  nglog::DynamicFlag<SlowAssignmentValue> flag("fork_value_flag", __FILE__, "",
                                               SlowAssignmentValue{});
  g_slow_assignment_entered = false;
  static std::atomic<bool> finished{false};
  finished = false;
  std::thread([&flag] {
    nglog::SetFlag(flag, SlowAssignmentValue{});
    finished = true;
  }).detach();
  while (!g_slow_assignment_entered) {
    std::this_thread::yield();
  }
  const pid_t child = fork();
  if (child == 0) {
    _exit(nglog::GetFlag(flag).value == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
  }
  while (!finished) {
    std::this_thread::yield();
  }
  ASSERT_NE(-1, child);
  EXPECT_TRUE(ChildSucceeded(child));
}
#endif

}  // namespace

int main(int argc, char** argv) {
  const ScopedEnvironmentVariable no_color{"NO_COLOR", nullptr};
  const ScopedEnvironmentVariable term{"TERM", "xterm"};
  const ScopedEnvironmentVariable clicolor_force{"CLICOLOR_FORCE", "1"};
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
