// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "ng-log/flags.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "internal/levenshtein.h"
#include "internal/process_flag_registry.h"
#include "testing_utilities.h"

#if !defined(NGLOG_OS_WINDOWS)
#  include <dirent.h>
#  include <sys/wait.h>
#  include <unistd.h>
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

template <>
struct FlagTraits<CustomFlagValue> {
  static const char* TypeName() noexcept { return "custom"; }

  static bool Parse(const std::string& value, CustomFlagValue* output,
                    std::string* error) {
    if (value == "7" || value == "9") {
      output->value = value[0] - '0';
      return true;
    }
    *error = "expected 7 or 9";
    return false;
  }

  static std::string ToString(const CustomFlagValue& value) {
    return std::to_string(value.value);
  }
};

template <>
struct FlagTraits<ConstructibleOnlyFlagValue> {
  static const char* TypeName() noexcept { return "constructible-only"; }

  static bool Parse(const std::string& value,
                    ConstructibleOnlyFlagValue* output, std::string* error) {
    if (value == "7" || value == "9") {
      output->value = value[0] - '0';
      return true;
    }
    *error = "expected 7 or 9";
    return false;
  }

  static std::string ToString(const ConstructibleOnlyFlagValue& value) {
    return std::to_string(value.value);
  }
};

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

int SetTestEnvironment(const char* name, const char* value) {
#ifdef NGLOG_OS_WINDOWS
  return _putenv_s(name, value);
#else
  return setenv(name, value, 1);
#endif
}

int UnsetTestEnvironment(const char* name) {
#ifdef NGLOG_OS_WINDOWS
  return _putenv_s(name, "");
#else
  return unsetenv(name);
#endif
}

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
  // Named differently from the base class, whose trivial destructor a call
  // through the name Flag would select.
  using Int32Flag = nglog::DynamicFlag<std::int32_t>;
  alignas(Int32Flag) unsigned char storage[sizeof(Int32Flag)];
  auto* flag = new (storage)
      Int32Flag("scoped_all_dynamic_flag", __FILE__, "Dynamic flag.", 1);
  Int32Flag* replacement = nullptr;
  {
    nglog::ScopedFlags<> save;
    flag->~Int32Flag();
    replacement = new (storage)
        Int32Flag("scoped_all_dynamic_flag", __FILE__, "Dynamic flag.", 3);
    EXPECT_EQ(3, nglog::GetFlag(*replacement));
  }
  EXPECT_EQ(3, nglog::GetFlag(*replacement));
  replacement->~Int32Flag();
}

TEST(Flags, ScopedFlagIgnoresDestroyedFlags) {
  // Named differently from the base class, whose trivial destructor a call
  // through the name Flag would select.
  using Int32Flag = nglog::DynamicFlag<std::int32_t>;
  alignas(Int32Flag) unsigned char storage[sizeof(Int32Flag)];
  auto* flag = new (storage)
      Int32Flag("scoped_selected_dynamic_flag", __FILE__, "Dynamic flag.", 1);
  Int32Flag* replacement = nullptr;
  {
    auto save = nglog::MakeScopedFlag(*flag, 2);
    flag->~Int32Flag();
    replacement = new (storage)
        Int32Flag("scoped_selected_dynamic_flag", __FILE__, "Dynamic flag.", 3);
    EXPECT_EQ(3, nglog::GetFlag(*replacement));
  }
  EXPECT_EQ(3, nglog::GetFlag(*replacement));
  replacement->~Int32Flag();
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
  nglog::SetFlag(FLAGS_test_int32_flag, 17);
  std::atomic<bool> stop{false};
  std::thread writer([&stop] {
    while (!stop.load(std::memory_order_relaxed)) {
      nglog::SetFlag(FLAGS_test_int32_flag, 17);
      nglog::SetFlag(FLAGS_test_int32_flag, 23);
    }
  });

  for (int iteration_count = 0; iteration_count < 1000; ++iteration_count) {
    const std::int32_t value = nglog::GetFlag(FLAGS_test_int32_flag);
    EXPECT_TRUE(value == 17 || value == 23);
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

  for (int iteration_count = 0; iteration_count < 1000; ++iteration_count) {
    const std::string value = nglog::GetFlag(FLAGS_test_string_flag);
    EXPECT_TRUE(value == "first" || value == "second");
  }

  stop.store(true, std::memory_order_relaxed);
  writer.join();
}

TEST(Flags, DefaultValuesCanBeAccessedConcurrently) {
  nglog::DynamicFlag<std::string> flag("concurrent_default_flag", __FILE__,
                                       "Concurrent default flag.", "first");
  std::atomic<bool> stop{false};
  std::atomic<int> invalid_values{0};
  std::thread writer([&flag, &stop] {
    while (!stop.load(std::memory_order_relaxed)) {
      std::string error;
      if (!flag.SetDefaultFromString("first", &error) ||
          !flag.SetDefaultFromString("second", &error)) {
        break;
      }
    }
  });

  for (int iteration_count = 0; iteration_count < 1000; ++iteration_count) {
    const std::string value = flag.DefaultValue();
    if (value != "first" && value != "second") {
      invalid_values.fetch_add(1, std::memory_order_relaxed);
    }
  }

  stop.store(true, std::memory_order_relaxed);
  writer.join();
  EXPECT_EQ(0, invalid_values.load(std::memory_order_relaxed));
}

TEST(Flags, ParsesTypedValues) {
  nglog::ScopedFlags<> save;
  char program[] = "flags_unittest";
  char bool_arg[] = "--test_bool_flag";
  char int_arg[] = "--test_int32_flag=-13";
  char uint_arg[] = "--test_uint32_flag=19";
  char string_arg[] = "--test_string_flag=changed";
  char* argv_storage[] = {program, bool_arg, int_arg, uint_arg, string_arg};
  char** argv = argv_storage;
  int argc = 5;

  ASSERT_EQ(4U, nglog::ParseCommandLineFlags(
                    &argc, &argv, nglog::FlagParsingMode::kRemoveFlags));
  EXPECT_TRUE(nglog::GetFlag(FLAGS_test_bool_flag));
  EXPECT_EQ(-13, nglog::GetFlag(FLAGS_test_int32_flag));
  EXPECT_EQ(19, nglog::GetFlag(FLAGS_test_uint32_flag));
  EXPECT_EQ("changed", nglog::GetFlag(FLAGS_test_string_flag));
  EXPECT_EQ(1, argc);
}

TEST(Flags, FlagCallbacksMayReadTheRegistry) {
  ReentrantFlag flag;
  std::string error;
  EXPECT_TRUE(nglog::SetCommandLineOption("reentrant_flag", "value", &error));
  EXPECT_THAT(error, testing::IsEmpty());
}

TEST(Flags, ParsesAnEmptyStringValue) {
  nglog::ScopedFlags<> save;
  char program[] = "flags_unittest";
  char string_arg[] = "--test_string_flag=";
  char* argv_storage[] = {program, string_arg};
  char** argv = argv_storage;
  int argc = 2;

  ASSERT_EQ(1U, nglog::ParseCommandLineFlags(
                    &argc, &argv, nglog::FlagParsingMode::kRemoveFlags));
  EXPECT_THAT(nglog::GetFlag(FLAGS_test_string_flag), testing::IsEmpty());
}

TEST(Flags, KeepsConsumedValuesWhenRequested) {
  nglog::ScopedFlags<> save;
  char program[] = "flags_unittest";
  char int_arg[] = "--test_int32_flag";
  char int_value[] = "-13";
  char positional_arg[] = "positional";
  char* argv_storage[] = {program, int_arg, int_value, positional_arg};
  char** argv = argv_storage;
  int argc = 4;

  ASSERT_EQ(1U, nglog::ParseCommandLineFlags(
                    &argc, &argv, nglog::FlagParsingMode::kKeepFlags));
  ASSERT_EQ(4, argc);
  EXPECT_THAT(argv[1], testing::StrEq("--test_int32_flag"));
  EXPECT_THAT(argv[2], testing::StrEq("-13"));
  EXPECT_THAT(argv[3], testing::StrEq("positional"));
}

TEST(Flags, ParsesDecimalNumbersWithLeadingZeroes) {
  nglog::ScopedFlags<> save;
  char program[] = "flags_unittest";
  char int_arg[] = "--test_int32_flag=010";
  char uint_arg[] = "--test_uint32_flag=08";
  char* argv_storage[] = {program, int_arg, uint_arg};
  char** argv = argv_storage;
  int argc = 3;

  ASSERT_EQ(2U, nglog::ParseCommandLineFlags(
                    &argc, &argv, nglog::FlagParsingMode::kRemoveFlags));
  EXPECT_EQ(10, nglog::GetFlag(FLAGS_test_int32_flag));
  EXPECT_EQ(8U, nglog::GetFlag(FLAGS_test_uint32_flag));
}

TEST(Flags, ParsesMixedCaseBooleanValues) {
  nglog::ScopedFlags<> save;
  char program[] = "flags_unittest";
  char true_arg[] = "--test_bool_flag=TrUe";
  char* argv_storage[] = {program, true_arg};
  char** argv = argv_storage;
  int argc = 2;

  ASSERT_EQ(1U, nglog::ParseCommandLineFlags(
                    &argc, &argv, nglog::FlagParsingMode::kRemoveFlags));
  EXPECT_TRUE(nglog::GetFlag(FLAGS_test_bool_flag));
}

TEST(Flags, ExposesTypedReflection) {
  EXPECT_THAT(FLAGS_test_bool_flag.type_name(), testing::StrEq("bool"));
  EXPECT_THAT(FLAGS_test_int32_flag.type_name(), testing::StrEq("int32"));
  EXPECT_THAT(FLAGS_test_uint32_flag.type_name(), testing::StrEq("uint32"));
  EXPECT_THAT(FLAGS_test_string_flag.type_name(), testing::StrEq("string"));
  EXPECT_EQ(false, nglog::GetFlag(FLAGS_test_bool_flag));
}

TEST(Flags, ExplicitFlagHelpRetainsPlainReflectionText) {
  std::vector<nglog::FlagInfo> flags;
  nglog::GetAllFlags(&flags);
  const auto flag = std::find_if(
      flags.begin(), flags.end(),
      [](const nglog::FlagInfo& info) { return info.name == "logtostderr"; });
  ASSERT_NE(flag, flags.end());
  ASSERT_TRUE(flag->help_function);
  EXPECT_THAT(
      flag->help,
      testing::StrEq("Send log messages to stderr instead of log files."));
}

TEST(Flags, BuiltInHelpUsesUserFriendlyWording) {
  std::vector<nglog::FlagInfo> flags;
  nglog::GetAllFlags(&flags);
  const auto find_help = [&flags](const char* name) {
    const auto flag = std::find_if(
        flags.begin(), flags.end(),
        [name](const nglog::FlagInfo& info) { return info.name == name; });
    EXPECT_NE(flag, flags.end());
    return flag == flags.end() ? std::string() : flag->help;
  };

  EXPECT_THAT(find_help("alsologtoemail"),
              testing::StrEq("Send log messages to each address in this "
                             "comma-separated list in addition to log files."));
  EXPECT_THAT(find_help("log_file_header"),
              testing::StrEq("Write a header at the beginning of each log "
                             "file."));
  EXPECT_THAT(
      find_help("logbufsecs"),
      testing::StrEq("Keep buffered log messages for no more than this many "
                     "seconds."));
  EXPECT_THAT(find_help("logfile_mode"),
              testing::StrEq("Set the permissions for newly created log "
                             "files."));
  EXPECT_THAT(
      find_help("max_log_size"),
      testing::StrEq("Limit each log file to approximately this size in MiB. "
                     "A value of 0 is treated as 1 and values above 4095 are "
                     "capped at 4095."));
  EXPECT_THAT(
      find_help("symbolize_line_info"),
      testing::StrEq("Resolve source file names and line numbers in stack "
                     "frames with addr2line or libbacktrace, when available."));
  EXPECT_THAT(
      find_help("symbolize_file_base_path"),
      testing::StrEq("Use this base path to resolve relative source paths when "
                     "creating terminal hyperlinks."));
  EXPECT_THAT(find_help("symbolize_hyperlinks"),
              testing::StrEq(
                  "Add hyperlinks to file:line references in colorized stack "
                  "traces when supported by the terminal. Set false to "
                  "disable them."));
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
  nglog::CaptureTestStderr();
  nglog::PrintCommandLineFlags();
  const std::string output = nglog::GetCapturedTestStderr();
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
  nglog::CaptureTestStderr();
  nglog::PrintCommandLineFlags();
  const std::string output = nglog::GetCapturedTestStderr();
  EXPECT_THAT(
      output,
      testing::HasSubstr(
          "Literal text mentions --not-a-real-option, TERM=dumb, stdout, "
          "and"));
  EXPECT_THAT(output, testing::HasSubstr("file:line."));
}

TEST(Flags, StyledHelpWrapsAcrossStyleBoundaries) {
  nglog::CaptureTestStderr();
  nglog::PrintCommandLineFlags();
  const std::string output = nglog::GetCapturedTestStderr();
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
  char program[] = "flags_unittest";
  char argument[] = "--test_bool_flga";
  char* argv_storage[] = {program, argument, nullptr};
  char** argv = argv_storage;
  int argc = 2;
  ASSERT_EXIT(nglog::ParseCommandLineFlags(
                  &argc, &argv, nglog::FlagParsingMode::kRemoveFlags),
              testing::ExitedWithCode(EXIT_FAILURE),
              "\033\\[1;31merror:\033\\[0m"
              ".*unknown command-line option");

  ASSERT_EXIT(nglog::ParseCommandLineFlags(
                  &argc, &argv, nglog::FlagParsingMode::kRemoveFlags),
              testing::ExitedWithCode(EXIT_FAILURE),
              "\033\\[2;39m    Did you mean \033\\[0m"
              "\033\\[39m--\033\\[0m\033\\[1;36mtest_bool_fl\033\\[0m"
              "\033\\[1;3;4;36mag\033\\[0m"
              "\033\\[2;39m\\?\033\\[0m");

  ASSERT_EXIT(nglog::ParseCommandLineFlags(
                  &argc, &argv, nglog::FlagParsingMode::kRemoveFlags),
              testing::ExitedWithCode(EXIT_FAILURE),
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
  ASSERT_EXIT(([] {
                char program[] = "flags_unittest";
                char argument[] = "--halp";
                char* argv_storage[] = {program, argument};
                char** argv = argv_storage;
                int argc = 2;
                nglog::ParseCommandLineFlags(
                    &argc, &argv, nglog::FlagParsingMode::kRemoveFlags);
              }()),
              testing::ExitedWithCode(EXIT_FAILURE),
              "\033\\[2;39m    Did you mean \033\\[0m"
              "\033\\[39m--\033\\[0m\033\\[1;36mh\033\\[0m"
              "\033\\[1;3;4;36me\033\\[0m\033\\[1;36mlp\033\\[0m"
              "\033\\[2;39m\\?\033\\[0m");
}
#endif

TEST(Flags, UnknownFlagsCanBeIgnoredExplicitly) {
  char program[] = "flags_unittest";
  char argument[] = "--flag_from_another_library";
  char* argv_storage[] = {program, argument, nullptr};
  char** argv = argv_storage;
  int argc = 2;
  EXPECT_EQ(0U, nglog::ParseCommandLineFlags(
                    &argc, &argv, nglog::FlagParsingMode::kRemoveFlags,
                    nglog::UnknownFlagMode::kIgnore));
  ASSERT_EQ(2, argc);
  EXPECT_THAT(argv[1], testing::StrEq("--flag_from_another_library"));
}

TEST(Flags, SeparatorIsRemovedWithTheFlags) {
  nglog::ScopedFlags<> save;
  char program[] = "flags_unittest";
  char flag[] = "--test_bool_flag";
  char separator[] = "--";
  char option_like[] = "--test_int32_flag=9";
  char repeated_separator[] = "--";
  char* argv_storage[] = {
      program, flag, separator, option_like, repeated_separator, nullptr};
  char** argv = argv_storage;
  int argc = 5;
  EXPECT_EQ(1U, nglog::ParseCommandLineFlags(
                    &argc, &argv, nglog::FlagParsingMode::kRemoveFlags));
  ASSERT_EQ(3, argc);
  EXPECT_THAT(argv[1], testing::StrEq("--test_int32_flag=9"));
  EXPECT_THAT(argv[2], testing::StrEq("--"));
  EXPECT_EQ(nullptr, argv[3]);
  EXPECT_EQ(7, nglog::GetFlag(FLAGS_test_int32_flag));
}

TEST(Flags, SeparatorIsKeptForAnotherParser) {
  nglog::ScopedFlags<> save;
  char program[] = "flags_unittest";
  char flag[] = "--test_bool_flag";
  char foreign[] = "--flag_from_another_library";
  char separator[] = "--";
  char option_like[] = "--not_an_option";
  char* argv_storage[] = {program,   flag,        foreign,
                          separator, option_like, nullptr};
  char** argv = argv_storage;
  int argc = 5;
  EXPECT_EQ(1U, nglog::ParseCommandLineFlags(
                    &argc, &argv, nglog::FlagParsingMode::kRemoveFlags,
                    nglog::UnknownFlagMode::kIgnore));
  ASSERT_EQ(4, argc);
  EXPECT_THAT(argv[1], testing::StrEq("--flag_from_another_library"));
  EXPECT_THAT(argv[2], testing::StrEq("--"));
  EXPECT_THAT(argv[3], testing::StrEq("--not_an_option"));
}

TEST(Flags, SeparatorIsKeptWhenFlagsAreKept) {
  nglog::ScopedFlags<> save;
  char program[] = "flags_unittest";
  char separator[] = "--";
  char positional[] = "file";
  char* argv_storage[] = {program, separator, positional, nullptr};
  char** argv = argv_storage;
  int argc = 3;
  EXPECT_EQ(0U, nglog::ParseCommandLineFlags(
                    &argc, &argv, nglog::FlagParsingMode::kKeepFlags));
  ASSERT_EQ(3, argc);
  EXPECT_THAT(argv[1], testing::StrEq("--"));
}

TEST(Flags, OptionsCanBeGrouped) {
  const nglog::FlagModule module("test-options", "Options provided by", "");
  const auto group = module.AddGroup("Test options");
  ASSERT_TRUE(group.AddFlag(FLAGS_test_wrapped_help_flag));

  std::vector<nglog::FlagInfo> flags;
  nglog::GetAllFlags(&flags);
  const auto flag =
      std::find_if(flags.begin(), flags.end(), [](const nglog::FlagInfo& info) {
        return info.name == "test_wrapped_help_flag";
      });
  ASSERT_NE(flag, flags.end());
  EXPECT_THAT(flag->group, testing::StrEq("Test options"));

  nglog::CaptureTestStderr();
  nglog::PrintCommandLineFlags();
  const std::string output = nglog::GetCapturedTestStderr();
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

  std::vector<nglog::FlagInfo> flags;
  nglog::GetAllFlags(&flags);
  const auto flag =
      std::find_if(flags.begin(), flags.end(), [](const nglog::FlagInfo& info) {
        return info.name == "test_custom_flag";
      });
  ASSERT_NE(flag, flags.end());
  EXPECT_THAT(flag->module, testing::StrEq("mandatory-group-module"));
  EXPECT_THAT(flag->group, testing::StrEq("Grouped options"));
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

  std::vector<nglog::FlagInfo> flags;
  nglog::GetAllFlags(&flags);
  const auto flag =
      std::find_if(flags.begin(), flags.end(), [](const nglog::FlagInfo& info) {
        return info.name == "test_bool_flag";
      });
  ASSERT_NE(flag, flags.end());
  EXPECT_THAT(flag->module, testing::StrEq("test-application"));

  nglog::CaptureTestStderr();
  nglog::PrintCommandLineFlags();
  const std::string output = nglog::GetCapturedTestStderr();
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

  nglog::CaptureTestStderr();
  nglog::PrintCommandLineFlags();
  const std::string output = nglog::GetCapturedTestStderr();
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
  ASSERT_EQ(0, SetTestEnvironment("DYNAMIC_default_flag", "23"));
  const nglog::FlagModule module("environment-module", "Options provided by",
                                 "DYNAMIC_");
  const auto group = module.AddGroup("Environment options");
  {
    nglog::DynamicFlag<std::int32_t> flag("default_flag", __FILE__,
                                          "Environment default flag.", 7);
    ASSERT_TRUE(group.AddFlag(flag));
    EXPECT_EQ(23, nglog::GetFlag(flag));

    std::vector<nglog::FlagInfo> flags;
    nglog::GetAllFlags(&flags);
    const auto found = std::find_if(flags.begin(), flags.end(),
                                    [](const nglog::FlagInfo& info) {
                                      return info.name == "default_flag";
                                    });
    ASSERT_NE(found, flags.end());
    EXPECT_THAT(found->default_value, testing::StrEq("23"));
  }
  ASSERT_EQ(0, UnsetTestEnvironment("DYNAMIC_default_flag"));
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
  std::vector<nglog::FlagInfo> flags;
  nglog::GetAllFlags(&flags);
  for (const nglog::FlagInfo& flag : flags) {
    ASSERT_FALSE(flag.help.empty()) << flag.name;
    EXPECT_THAT("ABCDEFGHIJKLMNOPQRSTUVWXYZ",
                testing::HasSubstr(flag.help.substr(0, 1)));
    EXPECT_THAT(flag.help, testing::EndsWith("."));
  }

  nglog::CaptureTestStderr();
  nglog::PrintCommandLineFlags();
  const std::string output = nglog::GetCapturedTestStderr();
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

  nglog::CaptureTestStderr();
  nglog::PrintCommandLineFlags();
  const std::string output = nglog::GetCapturedTestStderr();
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

  nglog::CaptureTestStderr();
  nglog::PrintCommandLineFlags();
  const std::string output = nglog::GetCapturedTestStderr();
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
  nglog::CaptureTestStderr();
  nglog::PrintCommandLineFlags();
  const std::string plain_output = nglog::GetCapturedTestStderr();
  EXPECT_THAT(plain_output,
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
        std::atomic<bool> completed{false};
        std::thread usage_thread([&completed] {
          nglog::ProgramUsage();
          completed.store(true, std::memory_order_release);
        });
        for (int attempt = 0;
             attempt < 100 && !completed.load(std::memory_order_acquire);
             ++attempt) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!completed.load(std::memory_order_acquire)) {
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
  char program[] = "flags_unittest";
  char argument[] = "--test_uint32_flag=-1";
  char* argv_storage[] = {program, argument};
  char** argv = argv_storage;
  int argc = 2;
  ASSERT_EXIT(nglog::ParseCommandLineFlags(
                  &argc, &argv, nglog::FlagParsingMode::kRemoveFlags),
              testing::ExitedWithCode(EXIT_FAILURE),
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
  ASSERT_EXIT(([] {
                char program[] = "flags_unittest";
                char argument[] = "--notest_int32_flag";
                char* argv_storage[] = {program, argument};
                char** argv = argv_storage;
                int argc = 2;
                nglog::ParseCommandLineFlags(
                    &argc, &argv, nglog::FlagParsingMode::kRemoveFlags);
              }()),
              testing::ExitedWithCode(EXIT_FAILURE),
              "cannot be negated because it is not boolean");
}
#endif

#ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, RejectsValuesForNegatedBooleanFlags) {
  ASSERT_EXIT(([] {
                char program[] = "flags_unittest";
                char argument[] = "--notest_bool_flag=true";
                char* argv_storage[] = {program, argument};
                char** argv = argv_storage;
                int argc = 2;
                nglog::ParseCommandLineFlags(
                    &argc, &argv, nglog::FlagParsingMode::kRemoveFlags);
              }()),
              testing::ExitedWithCode(EXIT_FAILURE),
              "does not accept a value when negated");
}
#endif

#ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, RejectsValuesForHelpOption) {
  ASSERT_EXIT(([] {
                char program[] = "flags_unittest";
                char argument[] = "--help=unexpected";
                char* argv_storage[] = {program, argument};
                char** argv = argv_storage;
                int argc = 2;
                nglog::ParseCommandLineFlags(
                    &argc, &argv, nglog::FlagParsingMode::kRemoveFlags);
              }()),
              testing::ExitedWithCode(EXIT_FAILURE),
              "command-line option --help does not accept a value");
}
#endif

#ifdef GTEST_HAS_DEATH_TEST
TEST(Flags, HelpOptionPrintsAndExits) {
  ASSERT_EXIT(([] {
                char program[] = "flags_unittest";
                char help_arg[] = "--help";
                char* argv_storage[] = {program, help_arg};
                char** argv = argv_storage;
                int argc = 2;
                nglog::ParseCommandLineFlags(
                    &argc, &argv, nglog::FlagParsingMode::kRemoveFlags);
              }()),
              (testing::ExitedWithCode(0)),
              "\033\\[39m  --\033\\[0m\033\\[1;36mhelp\033\\[0m\n"
              "\033\\[2;39m      Show available command-line options "
              "and exit.\033\\[0m");
}
#endif

std::size_t CountOccurrences(const std::string& text,
                             const std::string& pattern) {
  std::size_t count = 0;
  for (std::size_t position = text.find(pattern); position != std::string::npos;
       position = text.find(pattern, position + pattern.size())) {
    ++count;
  }
  return count;
}

std::string StripTerminalStyles(const std::string& text) {
  static const std::regex escape("\033\\[[0-9;]*m");
  return std::regex_replace(text, escape, "");
}

TEST(Flags, UnbrokenHelpWordIsPrintedOnce) {
  nglog::CaptureTestStderr();
  nglog::PrintCommandLineFlags();
  const std::string output =
      StripTerminalStyles(nglog::GetCapturedTestStderr());
  EXPECT_EQ(1U, CountOccurrences(output, "Unbroken_help_second_paragraph."));
  EXPECT_THAT(output,
              testing::HasSubstr("\n      Unbroken_help_second_paragraph.\n"));
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
    EXPECT_DEATH(
        {
          const nglog::DynamicFlag<bool> flag(name, "app/options.cc", "",
                                              false);
        },
        "is not a valid command-line option name")
        << name;
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
  ASSERT_EQ(0, SetTestEnvironment("EXPLICIT_explicit_flag", "23"));
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
  ASSERT_EQ(0, UnsetTestEnvironment("EXPLICIT_explicit_flag"));
}

TEST(Flags, DefaultsAreDeterminedWhenTheFlagIsRegistered) {
  ASSERT_EQ(0, SetTestEnvironment(kDefaultSourceVariable, "changed"));
  const std::string value = nglog::GetFlag(FLAGS_test_environment_default_flag);
  ASSERT_EQ(0, UnsetTestEnvironment(kDefaultSourceVariable));
  EXPECT_THAT(value, testing::StrEq("unset"));
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
  ASSERT_EQ(0, SetTestEnvironment("RESTORE_restored_single_flag", "23"));
  ASSERT_EQ(0, SetTestEnvironment("RESTORE_restored_all_flag", "29"));
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
  ASSERT_EQ(0, UnsetTestEnvironment("RESTORE_restored_single_flag"));
  ASSERT_EQ(0, UnsetTestEnvironment("RESTORE_restored_all_flag"));
}

TEST(Flags, IntegersRejectSurroundingWhitespace) {
  for (const char* const value : {" 5", "\t5", "5 ", " 0x5"}) {
    std::int32_t signed_value = 7;
    std::uint32_t unsigned_value = 11;
    std::string error;
    EXPECT_FALSE(
        nglog::FlagTraits<std::int32_t>::Parse(value, &signed_value, &error))
        << value;
    EXPECT_EQ(7, signed_value) << value;
    EXPECT_FALSE(
        nglog::FlagTraits<std::uint32_t>::Parse(value, &unsigned_value, &error))
        << value;
    EXPECT_EQ(11U, unsigned_value) << value;
  }
  std::uint32_t unsigned_value = 11;
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
    nglog::FileMode mode{0664};
    std::string error;
    nglog::internal::FlagParseError details;
    EXPECT_FALSE(nglog::FlagTraits<nglog::FileMode>::ParseWithDetails(
        value, &mode, &error, &details))
        << value;
    EXPECT_EQ(0664U, mode.value()) << value;
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
  nglog::CaptureTestStderr();
  nglog::PrintCommandLineFlags();
  const std::string output =
      StripTerminalStyles(nglog::GetCapturedTestStderr());
  std::smatch match;
  const std::regex entry("--logfile_mode=\\S+ \\(default=([^)]*)\\)");
  ASSERT_TRUE(std::regex_search(output, match, entry)) << output;
  const std::string displayed = match[1].str();

  std::string default_value;
  std::vector<nglog::FlagInfo> flags;
  nglog::GetAllFlags(&flags);
  for (const nglog::FlagInfo& flag : flags) {
    if (flag.name == "logfile_mode") {
      default_value = flag.default_value;
    }
  }

  ASSERT_TRUE(nglog::SetCommandLineOption("logfile_mode", displayed.c_str()));
  std::string current_value;
  ASSERT_TRUE(nglog::GetCommandLineOption("logfile_mode", &current_value));
  EXPECT_THAT(current_value, testing::StrEq(default_value))
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

  std::vector<nglog::FlagInfo> flags;
  nglog::GetAllFlags(&flags);
  const auto found =
      std::find_if(flags.begin(), flags.end(), [](const nglog::FlagInfo& info) {
        return info.name == "module_lifetime_flag";
      });
  ASSERT_NE(found, flags.end());
  EXPECT_THAT(found->module, testing::IsEmpty());
  EXPECT_THAT(found->group, testing::IsEmpty());

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
class TemporaryDirectoryOverride {
 public:
  explicit TemporaryDirectoryOverride(const std::string& path) {
    const char* const previous = std::getenv("TMPDIR");
    had_previous_ = previous != nullptr;
    if (had_previous_) {
      previous_ = previous;
    }
    SetTestEnvironment("TMPDIR", path.c_str());
  }

  TemporaryDirectoryOverride(const TemporaryDirectoryOverride&) = delete;
  TemporaryDirectoryOverride& operator=(const TemporaryDirectoryOverride&) =
      delete;

  ~TemporaryDirectoryOverride() {
    if (had_previous_) {
      SetTestEnvironment("TMPDIR", previous_.c_str());
    } else {
      UnsetTestEnvironment("TMPDIR");
    }
  }

 private:
  bool had_previous_ = false;
  std::string previous_;
};

TEST(Flags, ProcessFlagRegistryDoesNotUseTheEnvironment) {
  nglog::internal::ProcessFlagRegistry registry;
  nglog::internal::ProcessFlagInfo previous;
  ASSERT_EQ(nglog::internal::ProcessFlagRegistration::kRegistered,
            registry.Register("environment_free_flag", "flags.cc", "bool",
                              &previous));
  EXPECT_EQ(nullptr, std::getenv("NGLOG_PROCESS_FLAG_REGISTRY_V1"));
  registry.Unregister("environment_free_flag", "flags.cc");
}

TEST(Flags, ProcessFlagRegistryDoesNotRequireAWritableTemporaryDirectory) {
  const TemporaryDirectoryOverride override_tmpdir(
      "/nonexistent-nglog-temporary-directory");
  nglog::internal::ProcessFlagRegistry registry;
  nglog::internal::ProcessFlagInfo previous;
  EXPECT_EQ(nglog::internal::ProcessFlagRegistration::kRegistered,
            registry.Register("unwritable_tmpdir_flag", "flags.cc", "bool",
                              &previous));
  registry.Unregister("unwritable_tmpdir_flag", "flags.cc");
}

TEST(Flags, ProcessFlagRegistryLeavesNoFilesBehind) {
  char directory_template[] = "/tmp/nglog-registry-test-XXXXXX";
  const char* const directory = mkdtemp(directory_template);
  ASSERT_NE(directory, nullptr);
  {
    const TemporaryDirectoryOverride override_tmpdir(directory);
    nglog::internal::ProcessFlagRegistry registry;
    nglog::internal::ProcessFlagInfo previous;
    ASSERT_EQ(
        nglog::internal::ProcessFlagRegistration::kRegistered,
        registry.Register("leak_check_flag", "flags.cc", "bool", &previous));
    registry.Unregister("leak_check_flag", "flags.cc");
  }

  std::vector<std::string> remaining;
  DIR* const handle = opendir(directory);
  ASSERT_NE(handle, nullptr);
  while (const dirent* const entry = readdir(handle)) {
    const std::string name = entry->d_name;
    if (name != "." && name != "..") {
      remaining.push_back(name);
    }
  }
  closedir(handle);
  for (const std::string& name : remaining) {
    unlink((std::string(directory) + "/" + name).c_str());
  }
  rmdir(directory);
  EXPECT_THAT(remaining, testing::IsEmpty());
}
#endif

// Emscripten provides fork, which always fails.
#if defined(HAVE_FORK) && !defined(NGLOG_OS_EMSCRIPTEN)
constexpr int kChildTimeoutMilliseconds = 5000;
constexpr int kChildPollMilliseconds = 10;

// Waits for a forked child and reports whether it exited successfully before
// the timeout. A child that does not finish in time is killed.
bool ChildSucceeded(pid_t child) {
  for (int waited = 0; waited < kChildTimeoutMilliseconds;
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
std::atomic<bool> g_registry_reading_default_entered{false};

// Takes the registry lock while the default value is being computed.
std::int32_t RegistryReadingDefault() {
  g_registry_reading_default_entered = true;
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  std::string value;
  nglog::GetCommandLineOption("v", &value);
  return 1;
}

TEST(Flags, ForkWhileADefaultValueReadsTheRegistryDoesNotDeadlock) {
  // The scenario runs in a child, so that a deadlock cannot stall the test.
  const pid_t scenario = fork();
  if (scenario == 0) {
    static nglog::Flag<std::int32_t> flag(
        nglog::internal::kStaticFlag, "registry_reading_default_flag", __FILE__,
        "", &RegistryReadingDefault);
    nglog::RegisterFlag(flag);
    std::thread([] { nglog::GetFlag(flag); }).detach();
    while (!g_registry_reading_default_entered) {
      std::this_thread::yield();
    }
    const pid_t child = fork();
    if (child == 0) {
      _exit(EXIT_SUCCESS);
    }
    _exit(ChildSucceeded(child) ? EXIT_SUCCESS : EXIT_FAILURE);
  }
  ASSERT_NE(-1, scenario);
  EXPECT_TRUE(ChildSucceeded(scenario));
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
  UnsetTestEnvironment("NO_COLOR");
  if (SetTestEnvironment("TERM", "xterm") != 0) {
    return 1;
  }
  if (SetTestEnvironment("CLICOLOR_FORCE", "1") != 0) {
    return 1;
  }
  testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  UnsetTestEnvironment("CLICOLOR_FORCE");
  return result;
}
