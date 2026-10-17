// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cinttypes>
#include <cstddef>
#include <cstdint>  // IWYU pragma: keep
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "config.h"
#include "flag_display.h"
#include "levenshtein.h"
#include "ng-log/flags.h"
#include "ng-log/platform.h"
#include "process_flag_registry.h"
#include "styled_output.h"
#include "terminal_capabilities.h"
#include "theme.h"

#ifdef NGLOG_OS_WINDOWS
#  include <windows.h>
#elif !defined(NGLOG_OS_EMSCRIPTEN)
#  include <sys/ioctl.h>
#endif

#ifdef HAVE_FORK
#  include <pthread.h>
#endif

namespace nglog {

namespace internal {

struct HelpWriterContext {
  FILE* output;
  ColorMode mode;
  std::string* plain_text;
  std::vector<std::pair<HelpTextStyle, std::string>>* styled_text;
};

}  // namespace internal

struct HelpWriter::Impl {
  internal::HelpWriterContext* context;
};

namespace internal {

struct HelpWriterAccess {
  static void Invoke(const HelpMessageFunction& function,
                     HelpWriterContext* context) {
    HelpWriter::Impl impl{context};
    HelpWriter writer(&impl);
    function(writer);
  }

  static void InvokeFlag(const FlagHelpFunction& function,
                         HelpWriterContext* context, const FlagInfo& flag) {
    HelpWriter::Impl impl{context};
    HelpWriter writer(&impl);
    function(writer, flag);
  }
};

}  // namespace internal

namespace internal {

using FlagMap = std::map<std::string, FlagBase*>;

struct FlagDisplay {
  std::string type;
  std::string unit;
};

// The type of a flag as seen by one NGLOG_DECLARE declaration.
struct FlagDeclarationEntry {
  const FlagDeclaration* declaration;
  std::string type;
  std::string filename;
};

// The registration of one flag and its association with a module.
struct FlagEntry {
  std::uint64_t registration;
  std::string module;
  std::string group;
  const void* module_identity;
};
// A recursive mutex that a forked child can release. A recursive pthread
// mutex records the kernel thread identifier of its owner, which differs in
// the child, so the child could not unlock it after fork.
class RegistryMutex {
 public:
  RegistryMutex() = default;
  RegistryMutex(const RegistryMutex&) = delete;
  RegistryMutex& operator=(const RegistryMutex&) = delete;

  void lock();
  void unlock();

 private:
  std::mutex mutex_;
  std::atomic<std::thread::id> owner_{};
  std::size_t depth_ = 0;
};

void RegistryMutex::lock() {
  const std::thread::id self = std::this_thread::get_id();
  if (owner_.load(std::memory_order_relaxed) == self) {
    ++depth_;
    return;
  }
  mutex_.lock();
  owner_.store(self, std::memory_order_relaxed);
  depth_ = 1;
}

void RegistryMutex::unlock() {
  if (--depth_ == 0) {
    owner_.store(std::thread::id(), std::memory_order_relaxed);
    mutex_.unlock();
  }
}

struct Registry {
  RegistryMutex mutex;
  FlagMap flags;
  ProcessFlagRegistry process_flags;
  struct ModuleInfo {
    struct GroupInfo {
      std::string name;
      std::shared_ptr<const void> identity;
    };

    std::string header;
    std::string environment_prefix;
    std::shared_ptr<const void> identity;
    std::vector<GroupInfo> groups;
  };
  std::map<std::string, ModuleInfo> modules;
  std::vector<std::string> module_order;
  std::string usage;
  HelpMessageFunction usage_function;
  std::map<std::string, FlagHelpFunction> flag_help_formatters;
  std::map<std::string, FlagDisplay> flag_displays;
  std::map<const FlagBase*, FlagEntry> entries;
  // Types recorded by NGLOG_DECLARE, keyed by flag name.
  std::multimap<std::string, FlagDeclarationEntry> declarations;
  std::uint64_t next_registration = 1;
};

struct FlagRegistryAccess {
  static void InitializeValues(const FlagBase& flag) {
    flag.InitializeValues();
  }

  static bool ValueIsExplicitlySet(const FlagBase& flag) {
    return flag.ValueIsExplicitlySet();
  }

  static bool RestoreFromString(const FlagBase& flag, const std::string& value,
                                bool explicitly_set, std::string* error) {
    return flag.RestoreFromString(value, explicitly_set, error);
  }

  static void LockValuesForFork(const FlagBase& flag) {
    flag.LockValuesForFork();
  }

  static void UnlockValuesAfterFork(const FlagBase& flag) {
    flag.UnlockValuesAfterFork();
  }
};

Registry& GetRegistry();

#ifdef HAVE_FORK
// Takes every lock that protects flags before the process forks, so that the
// child never inherits a lock held by a thread that does not exist there.
void PrepareFork() {
  Registry& registry = GetRegistry();
  registry.mutex.lock();
  for (const auto& entry : registry.entries) {
    FlagRegistryAccess::LockValuesForFork(*entry.first);
  }
  LockProcessFlagRegistryForFork();
}

void ResumeAfterFork() {
  Registry& registry = GetRegistry();
  UnlockProcessFlagRegistryAfterFork();
  for (auto entry = registry.entries.rbegin(); entry != registry.entries.rend();
       ++entry) {
    FlagRegistryAccess::UnlockValuesAfterFork(*entry->first);
  }
  registry.mutex.unlock();
}
#endif

Registry* CreateRegistry() {
  // Flags remain usable during exit, so the registry is never destroyed. It
  // lives in the static storage of this library instead of on the heap, so
  // unloading a copy of the library leaves no allocation behind.
  alignas(Registry) static unsigned char storage[sizeof(Registry)];
  auto* const registry = new (storage) Registry;
#ifdef HAVE_FORK
  pthread_atfork(&PrepareFork, &ResumeAfterFork, &ResumeAfterFork);
#endif
  return registry;
}

Registry& GetRegistry() {
  static Registry* const registry = CreateRegistry();
  return *registry;
}

}  // namespace internal

namespace {

using internal::GetRegistry;
using internal::Registry;
using internal::RegistryMutex;

constexpr std::size_t kDefaultTerminalWidth = 80;
constexpr char kDefaultOptionIndent[] = "  ";
constexpr char kGroupedOptionIndent[] = "    ";
constexpr char kDefaultHelpIndent[] = "      ";
constexpr char kGroupedHelpIndent[] = "        ";
constexpr char kBuiltinHelpName[] = "help";
// Command-line options start with this prefix.
constexpr char kOptionPrefix[] = "--";
// Ends the options, so that later arguments are kept even if they start with
// the option prefix.
constexpr char kEndOfOptions[] = "--";
constexpr std::size_t kOptionPrefixLength = sizeof(kOptionPrefix) - 1;
// Negates a boolean option, as in --nologtostderr.
constexpr char kNegationPrefix[] = "no";
constexpr std::size_t kNegationPrefixLength = sizeof(kNegationPrefix) - 1;
constexpr char kBuiltinHelpDescription[] =
    "Show available command-line options and exit.";
constexpr char kDefaultModuleHeader[] = "Options provided by";
// The widths of the labels in the diagnostics for duplicate flags, for
// declarations that disagree with their definition, and for flags registered
// multiple times.
constexpr std::size_t kDuplicateLabelWidth = 8;
constexpr std::size_t kDeclarationLabelWidth = 10;
constexpr std::size_t kRegistrationLabelWidth = 9;
// The largest file mode, covering the permission bits and the setuid, setgid,
// and sticky bits.
constexpr std::uint32_t kMaximumFileMode = 07777;

void WriteStyled(const internal::ColorSpec& spec, internal::ColorMode mode,
                 const std::string& text);

std::string RenderPlainHelp(const HelpMessageFunction& function) {
  if (!function) {
    return {};
  }
  std::string text;
  internal::HelpWriterContext context{nullptr, internal::ColorMode::kNone,
                                      &text, nullptr};
  internal::HelpWriterAccess::Invoke(function, &context);
  return text;
}

void WriteErrorLabel(internal::ColorMode mode) {
  const internal::ColorSpec error_spec{internal::Color::kRed,
                                       internal::TextStyle::kBold,
                                       internal::Color::kDefault};
  WriteStyled(error_spec, mode, "error:");
}

[[noreturn]] void ExitFlagError(const std::string& message) {
  const internal::Theme& theme = internal::DefaultTheme();
  const internal::ColorMode mode = internal::StreamColorMode(stderr);
  WriteErrorLabel(mode);
  WriteStyled(theme.Get(internal::Role::kHelpText), mode, " " + message + "\n");
  std::fflush(stderr);
  std::exit(EXIT_FAILURE);
}

[[noreturn]] void AbortFlagError(const char* action, const FlagBase& flag,
                                 const std::string& error) {
  const internal::Theme& theme = internal::DefaultTheme();
  const internal::ColorSpec message_spec = theme.Get(internal::Role::kHelpText);
  const internal::ColorSpec option_spec =
      theme.Get(internal::Role::kHelpFlagName);
  const internal::ColorMode mode = internal::StreamColorMode(stderr);
  WriteErrorLabel(mode);
  WriteStyled(message_spec, mode, std::string(" ") + action + " ");
  WriteStyled(message_spec, mode, kOptionPrefix);
  WriteStyled(option_spec, mode, flag.name());
  WriteStyled(message_spec, mode, ": " + error + "\n");
  std::fflush(stderr);
  std::abort();
}

[[noreturn]] void ExitFlagError(const char* action,
                                const std::string& flag_name,
                                const std::string& type_name,
                                const internal::FlagParseError& details,
                                const std::string& error) {
  const internal::Theme& theme = internal::DefaultTheme();
  const internal::ColorSpec message_spec = theme.Get(internal::Role::kHelpText);
  const internal::ColorSpec option_spec =
      theme.Get(internal::Role::kHelpFlagName);
  const internal::ColorSpec type_spec =
      theme.Get(internal::Role::kHelpFlagType);
  const internal::ColorSpec value_spec =
      theme.Get(internal::Role::kHelpFlagValue);
  const internal::ColorMode mode = internal::StreamColorMode(stderr);
  WriteErrorLabel(mode);
  WriteStyled(message_spec, mode, std::string(" ") + action + " ");
  WriteStyled(message_spec, mode, kOptionPrefix);
  WriteStyled(option_spec, mode, flag_name);
  WriteStyled(message_spec, mode, ": invalid value ");
  WriteStyled(value_spec, mode, details.invalid_value);
  WriteStyled(message_spec, mode, " for ");
  WriteStyled(type_spec, mode, type_name);
  WriteStyled(message_spec, mode, " flag, ");
  if (!details.minimum.empty() && !details.maximum.empty()) {
    WriteStyled(message_spec, mode, "expected ");
    WriteStyled(value_spec, mode, details.minimum);
    WriteStyled(message_spec, mode, " through ");
    WriteStyled(value_spec, mode, details.maximum);
  } else if (!details.expected_values.empty()) {
    WriteStyled(message_spec, mode, "expected ");
    for (std::size_t index = 0; index < details.expected_values.size();
         ++index) {
      if (index != 0) {
        WriteStyled(
            message_spec, mode,
            index + 1 == details.expected_values.size() ? " or " : ", ");
      }
      WriteStyled(value_spec, mode, details.expected_values[index]);
    }
  } else if (!error.empty()) {
    WriteStyled(message_spec, mode, error);
  } else {
    WriteStyled(message_spec, mode, "expected a valid " + type_name);
  }
  WriteStyled(message_spec, mode, "\n");
  std::fflush(stderr);
  std::exit(EXIT_FAILURE);
}

void WriteSuggestedFlag(const std::string& misspelled,
                        const std::string& suggestion,
                        const internal::ColorSpec& option_spec,
                        const internal::ColorSpec& mismatch_spec,
                        internal::ColorMode mode) {
  const internal::ColorSpec prefix_spec =
      internal::DefaultTheme().Get(internal::Role::kHelpText);
  std::size_t common_prefix = 0;
  while (common_prefix < misspelled.size() &&
         common_prefix < suggestion.size() &&
         misspelled[common_prefix] == suggestion[common_prefix]) {
    ++common_prefix;
  }

  std::size_t common_suffix = 0;
  while (common_suffix + common_prefix < misspelled.size() &&
         common_suffix + common_prefix < suggestion.size() &&
         misspelled[misspelled.size() - common_suffix - 1] ==
             suggestion[suggestion.size() - common_suffix - 1]) {
    ++common_suffix;
  }

  const auto write_part = [mode](const internal::ColorSpec& spec,
                                 const std::string& text) {
    if (!text.empty()) {
      WriteStyled(spec, mode, text);
    }
  };
  write_part(prefix_spec, kOptionPrefix);
  write_part(option_spec, suggestion.substr(0, common_prefix));
  write_part(
      mismatch_spec,
      suggestion.substr(common_prefix,
                        suggestion.size() - common_prefix - common_suffix));
  write_part(option_spec, suggestion.substr(suggestion.size() - common_suffix));
}

[[noreturn]] void FatalUnknownFlag(const char* prefix, const std::string& name,
                                   const std::string& suggestion) {
  const internal::Theme& theme = internal::DefaultTheme();
  const internal::ColorSpec message_spec = theme.Get(internal::Role::kHelpText);
  const internal::ColorSpec suggestion_spec =
      theme.Get(internal::Role::kHelpDescription);
  const internal::ColorSpec option_spec =
      theme.Get(internal::Role::kHelpFlagName);
  internal::ColorSpec mismatch_spec = option_spec;
  mismatch_spec.style = mismatch_spec.style | internal::TextStyle::kItalic |
                        internal::TextStyle::kUnderline;
  const internal::ColorMode mode = internal::StreamColorMode(stderr);
  WriteErrorLabel(mode);
  WriteStyled(message_spec, mode, " unknown command-line option ");
  WriteStyled(message_spec, mode, prefix);
  WriteStyled(option_spec, mode, name);
  WriteStyled(message_spec, mode, ".\n");
  if (!suggestion.empty()) {
    WriteStyled(suggestion_spec, mode, "\n    Did you mean ");
    WriteSuggestedFlag(name, suggestion, option_spec, mismatch_spec, mode);
    WriteStyled(suggestion_spec, mode, "?\n");
  }
  WriteStyled(message_spec, mode, "\nUse ");
  WriteStyled(message_spec, mode, kOptionPrefix);
  WriteStyled(option_spec, mode, kBuiltinHelpName);
  WriteStyled(message_spec, mode, " to list all available options.\n");
  std::fflush(stderr);
  std::exit(EXIT_FAILURE);
}

std::string ErrorForRange(const std::string& expected) {
  return "expected " + expected;
}

bool ParseBoolean(const std::string& value, bool* output) {
  std::string normalized = value;
  for (char& character : normalized) {
    character =
        static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  if (normalized == "1" || normalized == "true" || normalized == "yes" ||
      normalized == "y" || normalized == "t") {
    *output = true;
    return true;
  }
  if (normalized == "0" || normalized == "false" || normalized == "no" ||
      normalized == "n" || normalized == "f") {
    *output = false;
    return true;
  }
  return false;
}

// Selects base 16 for a 0x or 0X prefix that follows an optional sign and base
// 10 otherwise. Returns 0 for empty text and for leading whitespace, which
// std::strtoimax and std::strtoumax would otherwise skip.
int IntegerBase(const std::string& value) {
  constexpr int kDecimalBase = 10;
  constexpr int kHexadecimalBase = 16;
  if (value.empty() ||
      std::isspace(static_cast<unsigned char>(value[0])) != 0) {
    return 0;
  }
  const std::size_t sign_offset = value[0] == '+' || value[0] == '-' ? 1 : 0;
  const bool hexadecimal =
      value.size() >= sign_offset + 2 && value[sign_offset] == '0' &&
      (value[sign_offset + 1] == 'x' || value[sign_offset + 1] == 'X');
  return hexadecimal ? kHexadecimalBase : kDecimalBase;
}

bool ParseSigned(const std::string& value, std::int32_t* output,
                 std::string* error) {
  constexpr std::int32_t kMinimum = std::numeric_limits<std::int32_t>::min();
  constexpr std::int32_t kMaximum = std::numeric_limits<std::int32_t>::max();
  const int base = IntegerBase(value);
  errno = 0;
  char* end = nullptr;
  const std::intmax_t parsed =
      base == 0 ? 0 : std::strtoimax(value.c_str(), &end, base);
  if (base == 0 || errno != 0 || end == value.c_str() || *end != '\0' ||
      parsed < kMinimum || parsed > kMaximum) {
    *error = ErrorForRange(std::to_string(kMinimum) + " through " +
                           std::to_string(kMaximum));
    return false;
  }
  *output = static_cast<std::int32_t>(parsed);
  return true;
}

bool ParseUnsigned(const std::string& value, std::uint32_t* output,
                   std::string* error) {
  constexpr std::uint32_t kMaximum = std::numeric_limits<std::uint32_t>::max();
  const int base = IntegerBase(value);
  errno = 0;
  char* end = nullptr;
  // std::strtoumax negates negative input instead of rejecting it.
  const std::uintmax_t parsed = base == 0 || value[0] == '-'
                                    ? 0
                                    : std::strtoumax(value.c_str(), &end, base);
  if (base == 0 || value[0] == '-' || errno != 0 || end == value.c_str() ||
      *end != '\0' || parsed > kMaximum) {
    *error = ErrorForRange("0 through " + std::to_string(kMaximum));
    return false;
  }
  *output = static_cast<std::uint32_t>(parsed);
  return true;
}

FlagBase* FindFlagLocked(const std::string& name) {
  Registry& registry = GetRegistry();
  const auto direct = registry.flags.find(name);
  if (direct != registry.flags.end()) {
    return direct->second;
  }
  // Flag names cannot start with a hyphen, which therefore does not stand for
  // a leading underscore.
  if (name.empty() || name.front() == '-' ||
      name.find('-') == std::string::npos) {
    return nullptr;
  }
  std::string normalized = name;
  std::replace(normalized.begin(), normalized.end(), '-', '_');
  const auto underscored = registry.flags.find(normalized);
  return underscored == registry.flags.end() ? nullptr : underscored->second;
}

// Returns whether the name selects the help option, a flag or the negation of
// a boolean flag.
bool IsLongOptionLocked(const std::string& name) {
  if (name == kBuiltinHelpName || FindFlagLocked(name) != nullptr) {
    return true;
  }
  if (name.compare(0, kNegationPrefixLength, kNegationPrefix) != 0) {
    return false;
  }
  const FlagBase* const flag =
      FindFlagLocked(name.substr(kNegationPrefixLength));
  return flag != nullptr &&
         std::strcmp(flag->type_name(), FlagTraits<bool>::TypeName()) == 0;
}

// Returns the registered option closest to the name, or an empty string if
// none is close enough.
std::string FindClosestOptionLocked(const std::string& name) {
  Registry& registry = GetRegistry();
  // Negated boolean options are candidates as well, so that a misspelled
  // negation suggests one. Every flag may come with its negation, besides the
  // built-in help.
  constexpr std::size_t kNamesPerFlag = 2;
  constexpr std::size_t kBuiltinNames = 1;
  std::vector<std::string> names;
  names.reserve((kNamesPerFlag * registry.flags.size()) + kBuiltinNames);
  names.emplace_back(kBuiltinHelpName);
  for (const auto& entry : registry.flags) {
    names.push_back(entry.first);
    if (std::strcmp(entry.second->type_name(), FlagTraits<bool>::TypeName()) ==
        0) {
      names.push_back(kNegationPrefix + entry.first);
    }
  }
  return internal::FindClosestMatch(name, names);
}

void WriteStyled(const internal::ColorSpec& spec, internal::ColorMode mode,
                 const std::string& text) {
  internal::WriteStyledField(
      spec, mode,
      [](const char* value, std::size_t length) {
        std::fwrite(value, 1, length, stderr);
      },
      text.c_str(), text.size());
}

internal::ColorSpec HelpStyleSpec(HelpTextStyle style) {
  const internal::Theme& theme = internal::DefaultTheme();
  switch (style) {
    case HelpTextStyle::kPlain:
      return theme.Get(internal::Role::kHelpDescription);
    case HelpTextStyle::kHeading:
      return theme.Get(internal::Role::kHelpSectionHeading);
    case HelpTextStyle::kUsageOptions:
      return theme.Get(internal::Role::kHelpText);
    case HelpTextStyle::kOptionPrefix:
      return theme.Get(internal::Role::kHelpText);
    case HelpTextStyle::kOption:
      return theme.Get(internal::Role::kHelpFlagName);
    case HelpTextStyle::kType:
      return theme.Get(internal::Role::kHelpFlagType);
    case HelpTextStyle::kValue:
      return theme.Get(internal::Role::kHelpFlagValue);
    case HelpTextStyle::kCode:
      return theme.Get(internal::Role::kHelpCode);
    case HelpTextStyle::kConstant:
      return theme.Get(internal::Role::kHelpFlagValue);
    case HelpTextStyle::kEnvironmentVariable:
      return theme.Get(internal::Role::kHelpEnvironmentVariable);
    case HelpTextStyle::kExecutable:
      return theme.Get(internal::Role::kHelpExecutable);
    case HelpTextStyle::kMeta:
      return theme.Get(internal::Role::kHelpMeta);
    case HelpTextStyle::kStream:
      return theme.Get(internal::Role::kHelpStream);
  }
  return theme.Get(internal::Role::kHelpText);
}

void WriteBuiltinHelpOption(HelpWriter& help, const FlagInfo&) {
  help.Write(HelpTextStyle::kOptionPrefix,
             std::string(kDefaultOptionIndent) + kOptionPrefix);
  help.Write(HelpTextStyle::kOption, kBuiltinHelpName);
  help.Write("\n");
  help.Write(HelpTextStyle::kPlain,
             std::string(kDefaultHelpIndent) + kBuiltinHelpDescription + "\n");
}

void GetAllFlagsLocked(std::vector<FlagInfo>* flags) {
  Registry& registry = GetRegistry();
  flags->clear();
  flags->reserve(registry.flags.size());
  for (const auto& entry : registry.flags) {
    const FlagBase& flag = *entry.second;
    const internal::FlagEntry& details = registry.entries.at(&flag);
    HelpMessageFunction help_function;
    if (flag.help_callback() != nullptr) {
      help_function = flag.help_callback();
    }
    flags->push_back(
        {flag.name(), flag.type_name(),
         help_function ? RenderPlainHelp(help_function) : flag.help(),
         flag.CurrentValue(), flag.DefaultValue(), flag.filename(),
         details.group, help_function, details.module});
  }
}

struct HelpSnapshot {
  std::vector<FlagInfo> flags;
  std::string usage;
  HelpMessageFunction usage_function;
  std::map<std::string, FlagHelpFunction> flag_help_formatters;
  std::map<std::string, internal::FlagDisplay> flag_displays;
  std::map<std::string, Registry::ModuleInfo> modules;
  std::vector<std::string> module_order;
};

void GetHelpSnapshotLocked(HelpSnapshot* snapshot) {
  Registry& registry = GetRegistry();
  GetAllFlagsLocked(&snapshot->flags);
  snapshot->usage = registry.usage;
  snapshot->usage_function = registry.usage_function;
  snapshot->flag_help_formatters = registry.flag_help_formatters;
  snapshot->flag_displays = registry.flag_displays;
  snapshot->modules = registry.modules;
  snapshot->module_order = registry.module_order;
  if (registry.flags.find(kBuiltinHelpName) == registry.flags.end()) {
    snapshot->flags.insert(snapshot->flags.begin(),
                           FlagInfo{kBuiltinHelpName,
                                    "",
                                    kBuiltinHelpDescription,
                                    "",
                                    "",
                                    "",
                                    "",
                                    {},
                                    ""});
    snapshot->flag_help_formatters[kBuiltinHelpName] = WriteBuiltinHelpOption;
  }
}

std::string DisplayType(const HelpSnapshot& snapshot, const FlagInfo& flag) {
  const auto display = snapshot.flag_displays.find(flag.name);
  if (display != snapshot.flag_displays.end() &&
      !display->second.type.empty()) {
    return display->second.type;
  }
  return flag.type;
}

std::string DisplayDefaultValue(const HelpSnapshot& snapshot,
                                const FlagInfo& flag) {
  if (flag.type == FlagTraits<std::string>::TypeName() &&
      flag.default_value.empty()) {
    return "\"\"";
  }
  const auto display = snapshot.flag_displays.find(flag.name);
  if (display != snapshot.flag_displays.end() &&
      !display->second.unit.empty()) {
    return flag.default_value + " " + display->second.unit;
  }
  return flag.default_value;
}

std::size_t GetTerminalWidth() {
#ifdef NGLOG_OS_WINDOWS
  const HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (handle != INVALID_HANDLE_VALUE && handle != nullptr &&
      GetConsoleScreenBufferInfo(handle, &info)) {
    constexpr int kInclusiveRangeAdjustment = 1;
    const int width = static_cast<int>(info.srWindow.Right) -
                      static_cast<int>(info.srWindow.Left) +
                      kInclusiveRangeAdjustment;
    if (width > 0) {
      return static_cast<std::size_t>(width);
    }
  }
#elif !defined(NGLOG_OS_EMSCRIPTEN)
  struct winsize info = {};
  if (ioctl(fileno(stderr), TIOCGWINSZ, &info) == 0 && info.ws_col != 0) {
    return static_cast<std::size_t>(info.ws_col);
  }
#endif
  return kDefaultTerminalWidth;
}

struct StyledHelpCharacter {
  HelpTextStyle style;
  char value;
};

std::vector<StyledHelpCharacter> FlattenStyledHelp(
    const std::vector<std::pair<HelpTextStyle, std::string>>& parts) {
  std::vector<StyledHelpCharacter> characters;
  for (const auto& part : parts) {
    for (const char character : part.second) {
      characters.push_back({part.first, character});
    }
  }
  return characters;
}

void WriteWrappedStyledHelp(
    const std::vector<std::pair<HelpTextStyle, std::string>>& parts,
    const char* help_indent, std::size_t line_width, internal::ColorMode mode) {
  const std::vector<StyledHelpCharacter> characters = FlattenStyledHelp(parts);
  const internal::ColorSpec help_spec =
      internal::DefaultTheme().Get(internal::Role::kHelpDescription);
  if (characters.empty()) {
    WriteStyled(help_spec, mode, help_indent);
    std::fputc('\n', stderr);
    return;
  }

  const auto write_line = [&help_spec, mode, help_indent, &characters](
                              std::size_t begin, std::size_t end) {
    WriteStyled(help_spec, mode, help_indent);
    std::size_t part_begin = begin;
    while (part_begin < end) {
      const HelpTextStyle style = characters[part_begin].style;
      std::size_t part_end = part_begin + 1;
      while (part_end < end && characters[part_end].style == style) {
        ++part_end;
      }
      std::string text;
      text.reserve(part_end - part_begin);
      for (std::size_t index = part_begin; index < part_end; ++index) {
        text.push_back(characters[index].value);
      }
      WriteStyled(HelpStyleSpec(style), mode, text);
      part_begin = part_end;
    }
    std::fputc('\n', stderr);
  };

  std::size_t position = 0;
  while (position < characters.size()) {
    const auto position_iterator =
        characters.begin() + static_cast<std::ptrdiff_t>(position);
    const auto newline = std::find_if(position_iterator, characters.end(),
                                      [](const StyledHelpCharacter& character) {
                                        return character.value == '\n';
                                      });
    const std::size_t paragraph_end =
        static_cast<std::size_t>(newline - characters.begin());
    if (position == paragraph_end) {
      write_line(position, position);
    }
    while (position < paragraph_end) {
      std::size_t line_end = std::min(position + line_width, paragraph_end);
      if (line_end < paragraph_end) {
        std::size_t space = line_end;
        while (space > position) {
          if (characters[space - 1].value == ' ') {
            line_end = space - 1;
            break;
          }
          --space;
        }
      }
      write_line(position, line_end);
      position = line_end;
      while (position < paragraph_end && characters[position].value == ' ') {
        ++position;
      }
    }
    if (newline == characters.end()) {
      break;
    }
    position = paragraph_end + 1;
  }
}

void PrintHelpSnapshot(const HelpSnapshot& snapshot) {
  const internal::ColorMode mode = internal::StreamColorMode(stderr);
  const internal::Theme& theme = internal::DefaultTheme();
  const internal::ColorSpec section_spec =
      theme.Get(internal::Role::kHelpSectionHeading);
  const internal::ColorSpec module_header_spec =
      theme.Get(internal::Role::kHelpModuleHeader);
  const internal::ColorSpec module_name_spec =
      theme.Get(internal::Role::kHelpModuleName);
  const internal::ColorSpec name_spec =
      theme.Get(internal::Role::kHelpFlagName);
  const internal::ColorSpec type_spec =
      theme.Get(internal::Role::kHelpFlagType);
  const internal::ColorSpec value_spec =
      theme.Get(internal::Role::kHelpFlagValue);
  const internal::ColorSpec default_spec = theme.Get(internal::Role::kHelpText);
  const internal::ColorSpec help_spec =
      theme.Get(internal::Role::kHelpDescription);
  const internal::ColorSpec environment_spec =
      theme.Get(internal::Role::kHelpEnvironmentVariable);
  const std::size_t terminal_width = GetTerminalWidth();

  if (snapshot.usage_function) {
    internal::HelpWriterContext context{stderr, mode, nullptr, nullptr};
    internal::HelpWriterAccess::Invoke(snapshot.usage_function, &context);
  } else if (!snapshot.usage.empty()) {
    WriteStyled(help_spec, mode, snapshot.usage);
    std::fputc('\n', stderr);
  }
  const auto print_flag = [&snapshot, mode, terminal_width, &default_spec,
                           &name_spec, &help_spec, &type_spec, &value_spec](
                              const FlagInfo& flag, const char* option_indent,
                              const char* help_indent) {
    const auto custom = snapshot.flag_help_formatters.find(flag.name);
    if (custom != snapshot.flag_help_formatters.end()) {
      internal::HelpWriterContext context{stderr, mode, nullptr, nullptr};
      internal::HelpWriterAccess::InvokeFlag(custom->second, &context, flag);
      return;
    }
    const std::size_t help_indent_width = std::strlen(help_indent);
    const std::size_t help_width = terminal_width > help_indent_width
                                       ? terminal_width - help_indent_width
                                       : std::size_t{1};
    const std::string default_value = DisplayDefaultValue(snapshot, flag);
    const std::string type = DisplayType(snapshot, flag);
    WriteStyled(default_spec, mode, std::string(option_indent) + kOptionPrefix);
    WriteStyled(name_spec, mode, flag.name);
    WriteStyled(help_spec, mode, "=");
    WriteStyled(type_spec, mode, type);
    WriteStyled(default_spec, mode, " (default=");
    WriteStyled(value_spec, mode, default_value);
    WriteStyled(default_spec, mode, ")\n");
    std::vector<std::pair<HelpTextStyle, std::string>> parts;
    if (flag.help_function) {
      internal::HelpWriterContext context{stderr, mode, nullptr, &parts};
      internal::HelpWriterAccess::Invoke(flag.help_function, &context);
    } else {
      parts.emplace_back(HelpTextStyle::kPlain, flag.help);
    }
    WriteWrappedStyledHelp(parts, help_indent, help_width, mode);
  };

  using GroupedFlags = std::map<std::string, std::vector<const FlagInfo*>>;
  std::map<std::string, GroupedFlags> module_groups;
  for (const FlagInfo& flag : snapshot.flags) {
    module_groups[flag.module][flag.group].push_back(&flag);
  }

  const auto print_groups = [&print_flag, &section_spec,
                             mode](const GroupedFlags& groups) {
    bool has_options = false;
    const auto ungrouped = groups.find("");
    if (ungrouped != groups.end()) {
      for (const FlagInfo* const flag : ungrouped->second) {
        print_flag(*flag, kDefaultOptionIndent, kDefaultHelpIndent);
      }
      has_options = !ungrouped->second.empty();
    }
    for (const auto& group : groups) {
      if (group.first.empty()) {
        continue;
      }
      if (has_options) {
        std::fputc('\n', stderr);
      }
      WriteStyled(section_spec, mode, group.first + ":");
      std::fputc('\n', stderr);
      for (const FlagInfo* const flag : group.second) {
        print_flag(*flag, kGroupedOptionIndent, kGroupedHelpIndent);
      }
      has_options = true;
    }

    return has_options;
  };

  bool has_output = false;
  const auto print_module =
      [&has_output, &snapshot, &module_header_spec, &module_name_spec, mode,
       &print_groups](const std::string& name, const GroupedFlags& groups) {
        if (!name.empty()) {
          if (has_output) {
            std::fputc('\n', stderr);
          }
          const auto module = snapshot.modules.find(name);
          const std::string header =
              module != snapshot.modules.end() && !module->second.header.empty()
                  ? module->second.header
                  : kDefaultModuleHeader;
          WriteStyled(module_header_spec, mode, header + " ");
          WriteStyled(module_name_spec, mode, name);
          WriteStyled(module_header_spec, mode, ":\n\n");
          has_output = true;
        }
        const bool has_options = print_groups(groups);
        if (has_options) {
          has_output = true;
        }
      };

  const auto unassigned = module_groups.find("");
  if (unassigned != module_groups.end()) {
    print_module(unassigned->first, unassigned->second);
  }
  std::set<std::string> printed_modules;
  for (const std::string& name : snapshot.module_order) {
    const auto module = module_groups.find(name);
    if (module != module_groups.end() && !name.empty()) {
      print_module(module->first, module->second);
      printed_modules.insert(name);
    }
  }
  for (const auto& module : module_groups) {
    if (!module.first.empty() && printed_modules.count(module.first) == 0) {
      print_module(module.first, module.second);
    }
  }

  std::fputc('\n', stderr);
  WriteStyled(section_spec, mode, "Environment variables:");
  std::fputc('\n', stderr);
  std::set<std::string> emitted_environment_prefixes;
  const auto print_environment_prefix = [&module_groups, &snapshot,
                                         &emitted_environment_prefixes,
                                         &help_spec, &environment_spec,
                                         &name_spec, &value_spec,
                                         &module_name_spec,
                                         mode](const std::string& name) {
    const auto module = module_groups.find(name);
    if (module == module_groups.end() || name.empty()) {
      return;
    }
    const auto info = snapshot.modules.find(name);
    if (info == snapshot.modules.end() ||
        info->second.environment_prefix.empty() ||
        !emitted_environment_prefixes.insert(info->second.environment_prefix)
             .second) {
      return;
    }
    WriteStyled(help_spec, mode, "  ");
    WriteStyled(environment_spec, mode, info->second.environment_prefix);
    WriteStyled(name_spec, mode, "<flag>");
    WriteStyled(help_spec, mode, "=");
    WriteStyled(value_spec, mode, "<value>");
    WriteStyled(help_spec, mode, "  Sets ");
    WriteStyled(module_name_spec, mode, name);
    WriteStyled(help_spec, mode,
                " flag default. Command-line options take precedence.\n");
  };
  for (const std::string& name : snapshot.module_order) {
    print_environment_prefix(name);
  }
  for (const auto& module : module_groups) {
    print_environment_prefix(module.first);
  }
  std::fflush(stderr);
}

void PrintHelp() {
  Registry& registry = GetRegistry();
  HelpSnapshot snapshot;
  {
    const std::lock_guard<RegistryMutex> lock(registry.mutex);
    GetHelpSnapshotLocked(&snapshot);
  }
  PrintHelpSnapshot(snapshot);
}

}  // namespace

void HelpWriter::Write(const std::string& text) {
  if (impl_->context->plain_text != nullptr) {
    impl_->context->plain_text->append(text);
    return;
  }
  if (impl_->context->styled_text != nullptr) {
    impl_->context->styled_text->emplace_back(HelpTextStyle::kPlain, text);
    return;
  }
  WriteStyled(HelpStyleSpec(HelpTextStyle::kPlain), impl_->context->mode, text);
}

void HelpWriter::Write(HelpTextStyle style, const std::string& text) {
  if (impl_->context->plain_text != nullptr) {
    impl_->context->plain_text->append(text);
    return;
  }
  if (impl_->context->styled_text != nullptr) {
    impl_->context->styled_text->emplace_back(style, text);
    return;
  }
  WriteStyled(HelpStyleSpec(style), impl_->context->mode, text);
}

const char* FlagTraits<bool>::TypeName() noexcept { return "bool"; }

bool FlagTraits<bool>::Parse(const std::string& value, bool* output,
                             std::string* error) {
  if (ParseBoolean(value, output)) {
    return true;
  }
  *error = ErrorForRange("true or false");
  return false;
}

bool FlagTraits<bool>::ParseWithDetails(const std::string& value, bool* output,
                                        std::string* error,
                                        internal::FlagParseError* details) {
  if (Parse(value, output, error)) {
    return true;
  }
  if (details != nullptr) {
    details->expected_values = {"true", "false"};
  }
  return false;
}

std::string FlagTraits<bool>::ToString(bool value) {
  return value ? "true" : "false";
}

const char* FlagTraits<std::int32_t>::TypeName() noexcept { return "int32"; }

bool FlagTraits<std::int32_t>::Parse(const std::string& value,
                                     std::int32_t* output, std::string* error) {
  return ParseSigned(value, output, error);
}

bool FlagTraits<std::int32_t>::ParseWithDetails(
    const std::string& value, std::int32_t* output, std::string* error,
    internal::FlagParseError* details) {
  if (Parse(value, output, error)) {
    return true;
  }
  if (details != nullptr) {
    details->minimum = std::to_string(std::numeric_limits<std::int32_t>::min());
    details->maximum = std::to_string(std::numeric_limits<std::int32_t>::max());
  }
  return false;
}

std::string FlagTraits<std::int32_t>::ToString(std::int32_t value) {
  return std::to_string(value);
}

const char* FlagTraits<std::uint32_t>::TypeName() noexcept { return "uint32"; }

bool FlagTraits<std::uint32_t>::Parse(const std::string& value,
                                      std::uint32_t* output,
                                      std::string* error) {
  return ParseUnsigned(value, output, error);
}

bool FlagTraits<std::uint32_t>::ParseWithDetails(
    const std::string& value, std::uint32_t* output, std::string* error,
    internal::FlagParseError* details) {
  if (Parse(value, output, error)) {
    return true;
  }
  if (details != nullptr) {
    details->minimum = "0";
    details->maximum =
        std::to_string(std::numeric_limits<std::uint32_t>::max());
  }
  return false;
}

std::string FlagTraits<std::uint32_t>::ToString(std::uint32_t value) {
  return std::to_string(value);
}

const char* FlagTraits<FileMode>::TypeName() noexcept { return "mode"; }

bool FlagTraits<FileMode>::Parse(const std::string& value, FileMode* output,
                                 std::string* error) {
  constexpr std::uint32_t kOctalBase = 8;
  std::uint32_t mode = 0;
  bool valid = !value.empty();
  for (const char digit : value) {
    if (digit < '0' || digit > '7') {
      valid = false;
      break;
    }
    mode = mode * kOctalBase + static_cast<std::uint32_t>(digit - '0');
    if (mode > kMaximumFileMode) {
      valid = false;
      break;
    }
  }
  if (!valid) {
    *error = ErrorForRange(ToString(FileMode{0}) + " through " +
                           ToString(FileMode{kMaximumFileMode}) + " in octal");
    return false;
  }
  *output = FileMode{mode};
  return true;
}

bool FlagTraits<FileMode>::ParseWithDetails(const std::string& value,
                                            FileMode* output,
                                            std::string* error,
                                            internal::FlagParseError* details) {
  if (Parse(value, output, error)) {
    return true;
  }
  if (details != nullptr) {
    details->minimum = ToString(FileMode{0});
    details->maximum = ToString(FileMode{kMaximumFileMode});
  }
  return false;
}

std::string FlagTraits<FileMode>::ToString(FileMode value) {
  // A leading zero marks the value as octal, as in chmod and C literals.
  constexpr int kMinimumDigits = 3;
  std::ostringstream text;
  // Digit grouping of the global locale would produce text Parse() rejects.
  text.imbue(std::locale::classic());
  text << '0' << std::oct << std::setfill('0') << std::setw(kMinimumDigits)
       << value.value();
  return text.str();
}

const char* FlagTraits<std::string>::TypeName() noexcept { return "string"; }

bool FlagTraits<std::string>::Parse(const std::string& value,
                                    std::string* output, std::string*) {
  *output = value;
  return true;
}

std::string FlagTraits<std::string>::ToString(const std::string& value) {
  return value;
}

// Identifies where the earlier definition of a duplicate flag lives.
enum class DuplicateFlagOrigin {
  kThisLibrary,
  kOtherLibraryCopy,
};

// Explains why a flag was defined twice and how to resolve it. Line breaks
// keep the text within an 80 column terminal next to its label.
struct DuplicateFlagAdvice {
  const char* reason;
  const char* hint;
};

DuplicateFlagAdvice AdviseOnDuplicateFlag(DuplicateFlagOrigin origin,
                                          bool same_file) {
  switch (origin) {
    case DuplicateFlagOrigin::kThisLibrary:
      if (same_file) {
        return {
            "The same source file is compiled into more than one library\n"
            "or executable of this program.",
            "Compile the file into only one of them and access the option\n"
            "from the others with NGLOG_DECLARE."};
      }
      return {"Two definitions in this program use the same option name.",
              "Rename one of the options, or keep a single definition and\n"
              "access it elsewhere with NGLOG_DECLARE."};
    case DuplicateFlagOrigin::kOtherLibraryCopy:
      return {
          "Another copy of ng-log in this process already defines this\n"
          "option. This happens when several libraries each link ng-log\n"
          "statically.",
          "Link all libraries and the executable against a single shared\n"
          "ng-log library."};
  }
  return {"", ""};
}

// Writes a diagnostic detail such as "  Reason: text". Continuation lines are
// aligned with the first line of the text.
void WriteDiagnosticDetail(const internal::ColorSpec& label_spec,
                           const internal::ColorSpec& text_spec,
                           internal::ColorMode mode, std::size_t label_width,
                           const char* label, const std::string& text) {
  constexpr char kDetailIndent[] = "  ";
  std::string padded_label = std::string(label) + ":";
  padded_label.resize(std::max(label_width, padded_label.size()), ' ');
  const std::string continuation =
      "\n" + std::string(std::strlen(kDetailIndent) + padded_label.size(), ' ');
  std::string indented;
  for (const char character : text) {
    if (character == '\n') {
      indented += continuation;
    } else {
      indented.push_back(character);
    }
  }
  WriteStyled(label_spec, mode, kDetailIndent + padded_label);
  WriteStyled(text_spec, mode, indented);
  std::fputc('\n', stderr);
}

[[noreturn]] void AbortDuplicateFlag(const FlagBase& flag,
                                     const char* previous_filename,
                                     const char* previous_type,
                                     DuplicateFlagOrigin origin) {
  const internal::Theme& theme = internal::DefaultTheme();
  const internal::ColorMode mode = internal::StreamColorMode(stderr);
  const internal::ColorSpec text_spec = theme.Get(internal::Role::kHelpText);
  const internal::ColorSpec option_spec =
      theme.Get(internal::Role::kHelpFlagName);
  const internal::ColorSpec meta_spec = theme.Get(internal::Role::kHelpMeta);
  const DuplicateFlagAdvice advice = AdviseOnDuplicateFlag(
      origin, std::strcmp(previous_filename, flag.filename()) == 0);
  WriteErrorLabel(mode);
  WriteStyled(text_spec, mode, " command-line option ");
  WriteStyled(text_spec, mode, kOptionPrefix);
  WriteStyled(option_spec, mode, flag.name());
  WriteStyled(text_spec, mode, " was defined more than once.\n");
  WriteDiagnosticDetail(
      text_spec, meta_spec, mode, kDuplicateLabelWidth, "First",
      std::string(previous_filename) + " (" + previous_type + ")");
  WriteDiagnosticDetail(
      text_spec, meta_spec, mode, kDuplicateLabelWidth, "Second",
      std::string(flag.filename()) + " (" + flag.type_name() + ")");
  WriteDiagnosticDetail(text_spec, text_spec, mode, kDuplicateLabelWidth,
                        "Reason", advice.reason);
  WriteDiagnosticDetail(text_spec, text_spec, mode, kDuplicateLabelWidth,
                        "Hint", advice.hint);
  std::fflush(stderr);
  std::abort();
}

[[noreturn]] void AbortMismatchedDeclaration(
    const std::string& name, const std::string& declaration_filename,
    const std::string& declaration_type, const FlagBase& definition) {
  const internal::Theme& theme = internal::DefaultTheme();
  const internal::ColorMode mode = internal::StreamColorMode(stderr);
  const internal::ColorSpec text_spec = theme.Get(internal::Role::kHelpText);
  const internal::ColorSpec option_spec =
      theme.Get(internal::Role::kHelpFlagName);
  const internal::ColorSpec meta_spec = theme.Get(internal::Role::kHelpMeta);
  WriteErrorLabel(mode);
  WriteStyled(text_spec, mode, " command-line option ");
  WriteStyled(text_spec, mode, kOptionPrefix);
  WriteStyled(option_spec, mode, name);
  WriteStyled(text_spec, mode,
              " is declared with a different type than its definition.\n");
  WriteDiagnosticDetail(text_spec, meta_spec, mode, kDeclarationLabelWidth,
                        "Declared",
                        declaration_filename + " (" + declaration_type + ")");
  WriteDiagnosticDetail(
      text_spec, meta_spec, mode, kDeclarationLabelWidth, "Defined",
      std::string(definition.filename()) + " (" + definition.type_name() + ")");
  WriteDiagnosticDetail(
      text_spec, text_spec, mode, kDeclarationLabelWidth, "Reason",
      "Code that uses the declaration would access the flag through the\n"
      "wrong type.");
  WriteDiagnosticDetail(text_spec, text_spec, mode, kDeclarationLabelWidth,
                        "Hint",
                        "Use the same type in NGLOG_DECLARE and NGLOG_DEFINE.");
  std::fflush(stderr);
  std::abort();
}

[[noreturn]] void AbortRepeatedRegistration(const FlagBase& flag) {
  const internal::Theme& theme = internal::DefaultTheme();
  const internal::ColorMode mode = internal::StreamColorMode(stderr);
  const internal::ColorSpec text_spec = theme.Get(internal::Role::kHelpText);
  const internal::ColorSpec option_spec =
      theme.Get(internal::Role::kHelpFlagName);
  const internal::ColorSpec meta_spec = theme.Get(internal::Role::kHelpMeta);
  WriteErrorLabel(mode);
  WriteStyled(text_spec, mode, " command-line option ");
  WriteStyled(text_spec, mode, kOptionPrefix);
  WriteStyled(option_spec, mode, flag.name());
  WriteStyled(text_spec, mode, " was registered more than once.\n");
  WriteDiagnosticDetail(
      text_spec, meta_spec, mode, kRegistrationLabelWidth, "Defined",
      std::string(flag.filename()) + " (" + flag.type_name() + ")");
  WriteDiagnosticDetail(
      text_spec, text_spec, mode, kRegistrationLabelWidth, "Reason",
      "The same flag object was registered multiple times. This happens\n"
      "when a flag definition is linked into several libraries that\n"
      "resolve it to one object, or when RegisterFlag is called for a flag\n"
      "that NGLOG_DEFINE or DynamicFlag already registers.");
  WriteDiagnosticDetail(
      text_spec, text_spec, mode, kRegistrationLabelWidth, "Hint",
      "Link each flag definition into exactly one library, and remove\n"
      "the extra RegisterFlag call.");
  std::fflush(stderr);
  std::abort();
}

// Option names are identifiers, so that the parser can tell them apart from
// values and from the separator between a name and its value.
bool IsValidFlagName(const char* name) {
  if (name[0] == '\0' ||
      (std::isalpha(static_cast<unsigned char>(name[0])) == 0 &&
       name[0] != '_')) {
    return false;
  }
  for (const char* character = name; *character != '\0'; ++character) {
    if (std::isalnum(static_cast<unsigned char>(*character)) == 0 &&
        *character != '_') {
      return false;
    }
  }
  return true;
}

[[noreturn]] void AbortInvalidFlagName(const FlagBase& flag) {
  const internal::Theme& theme = internal::DefaultTheme();
  const internal::ColorMode mode = internal::StreamColorMode(stderr);
  const internal::ColorSpec text_spec = theme.Get(internal::Role::kHelpText);
  const internal::ColorSpec option_spec =
      theme.Get(internal::Role::kHelpFlagName);
  const internal::ColorSpec meta_spec = theme.Get(internal::Role::kHelpMeta);
  WriteErrorLabel(mode);
  WriteStyled(text_spec, mode, " \"");
  WriteStyled(option_spec, mode, flag.name());
  WriteStyled(text_spec, mode, "\" is not a valid command-line option name.\n");
  WriteDiagnosticDetail(
      text_spec, meta_spec, mode, kRegistrationLabelWidth, "Defined",
      std::string(flag.filename()) + " (" + flag.type_name() + ")");
  WriteDiagnosticDetail(
      text_spec, text_spec, mode, kRegistrationLabelWidth, "Reason",
      "The option could not be passed on the command line. A name must\n"
      "start with a letter or an underscore and contain only letters,\n"
      "digits, and underscores.");
  WriteDiagnosticDetail(text_spec, text_spec, mode, kRegistrationLabelWidth,
                        "Hint", "Rename the option.");
  std::fflush(stderr);
  std::abort();
}

// A name of the form noX is how the command line disables the boolean flag X.
[[noreturn]] void AbortNegationConflict(const FlagBase& negation,
                                        const FlagBase& flag) {
  const internal::Theme& theme = internal::DefaultTheme();
  const internal::ColorMode mode = internal::StreamColorMode(stderr);
  const internal::ColorSpec text_spec = theme.Get(internal::Role::kHelpText);
  const internal::ColorSpec option_spec =
      theme.Get(internal::Role::kHelpFlagName);
  const internal::ColorSpec meta_spec = theme.Get(internal::Role::kHelpMeta);
  WriteErrorLabel(mode);
  WriteStyled(text_spec, mode,
              std::string(" command-line option ") + kOptionPrefix);
  WriteStyled(option_spec, mode, negation.name());
  WriteStyled(text_spec, mode,
              std::string(" conflicts with the negation of ") + kOptionPrefix);
  WriteStyled(option_spec, mode, flag.name());
  WriteStyled(text_spec, mode, ".\n");
  WriteDiagnosticDetail(
      text_spec, meta_spec, mode, kRegistrationLabelWidth, "Option",
      std::string(negation.filename()) + " (" + negation.type_name() + ")");
  WriteDiagnosticDetail(
      text_spec, meta_spec, mode, kRegistrationLabelWidth, "Boolean",
      std::string(flag.filename()) + " (" + flag.type_name() + ")");
  WriteDiagnosticDetail(
      text_spec, text_spec, mode, kRegistrationLabelWidth, "Reason",
      std::string(kOptionPrefix) + negation.name() + " disables " +
          kOptionPrefix + flag.name() +
          " on the command line. An option with\n"
          "that name would make the meaning depend on which options exist.");
  WriteDiagnosticDetail(text_spec, text_spec, mode, kRegistrationLabelWidth,
                        "Hint", "Rename one of the options.");
  std::fflush(stderr);
  std::abort();
}

[[noreturn]] void AbortUnavailableProcessFlagRegistry() {
  const internal::Theme& theme = internal::DefaultTheme();
  const internal::ColorMode mode = internal::StreamColorMode(stderr);
  const internal::ColorSpec text_spec = theme.Get(internal::Role::kHelpText);
  WriteErrorLabel(mode);
  WriteStyled(text_spec, mode,
              " could not coordinate command-line option definitions "
              "between library instances.\n");
  std::fflush(stderr);
  std::abort();
}

[[noreturn]] void AbortReservedFlag(const FlagBase& flag) {
  const internal::Theme& theme = internal::DefaultTheme();
  const internal::ColorMode mode = internal::StreamColorMode(stderr);
  const internal::ColorSpec text_spec = theme.Get(internal::Role::kHelpText);
  const internal::ColorSpec option_spec =
      theme.Get(internal::Role::kHelpFlagName);
  WriteErrorLabel(mode);
  WriteStyled(text_spec, mode, " command-line option ");
  WriteStyled(text_spec, mode, kOptionPrefix);
  WriteStyled(option_spec, mode, flag.name());
  WriteStyled(text_spec, mode, " is reserved for the built-in help option.\n");
  std::fflush(stderr);
  std::abort();
}

[[noreturn]] void AbortDuplicateModule(const std::string& name) {
  const internal::Theme& theme = internal::DefaultTheme();
  const internal::ColorMode mode = internal::StreamColorMode(stderr);
  const internal::ColorSpec text_spec = theme.Get(internal::Role::kHelpText);
  const internal::ColorSpec module_spec =
      theme.Get(internal::Role::kHelpModuleName);
  WriteErrorLabel(mode);
  WriteStyled(text_spec, mode, " flag module ");
  WriteStyled(module_spec, mode, name);
  WriteStyled(text_spec, mode,
              " was registered more than once. Each flag module must have a "
              "unique name.\n");
  std::fflush(stderr);
  std::abort();
}

void RegisterFlag(FlagBase& flag) {
  // Determines the default when the flag is registered rather than on first
  // use. The default value function runs before the registry is locked.
  internal::FlagRegistryAccess::InitializeValues(flag);
  Registry& registry = GetRegistry();
  const std::lock_guard<RegistryMutex> lock(registry.mutex);
  if (registry.entries.count(&flag) != 0) {
    AbortRepeatedRegistration(flag);
  }
  if (!IsValidFlagName(flag.name())) {
    AbortInvalidFlagName(flag);
  }
  if (std::strcmp(flag.name(), kBuiltinHelpName) == 0) {
    AbortReservedFlag(flag);
  }
  if (std::strcmp(flag.type_name(), FlagTraits<bool>::TypeName()) == 0) {
    const auto negation =
        registry.flags.find(std::string(kNegationPrefix) + flag.name());
    if (negation != registry.flags.end()) {
      AbortNegationConflict(*negation->second, flag);
    }
  }
  if (std::strncmp(flag.name(), kNegationPrefix, kNegationPrefixLength) == 0) {
    const auto negated =
        registry.flags.find(flag.name() + kNegationPrefixLength);
    if (negated != registry.flags.end() &&
        std::strcmp(negated->second->type_name(),
                    FlagTraits<bool>::TypeName()) == 0) {
      AbortNegationConflict(flag, *negated->second);
    }
  }
  const auto inserted = registry.flags.emplace(flag.name(), &flag);
  if (!inserted.second) {
    const FlagBase& previous = *inserted.first->second;
    AbortDuplicateFlag(flag, previous.filename(), previous.type_name(),
                       DuplicateFlagOrigin::kThisLibrary);
  }
  internal::ProcessFlagInfo previous;
  const internal::ProcessFlagRegistration registration =
      registry.process_flags.Register(flag.name(), flag.filename(),
                                      flag.type_name(), &previous);
  if (registration == internal::ProcessFlagRegistration::kDuplicate) {
    AbortDuplicateFlag(flag, previous.filename.c_str(), previous.type.c_str(),
                       DuplicateFlagOrigin::kOtherLibraryCopy);
  }
  if (registration == internal::ProcessFlagRegistration::kUnavailable) {
    AbortUnavailableProcessFlagRegistry();
  }
  registry.entries.emplace(
      &flag,
      internal::FlagEntry{registry.next_registration++, {}, {}, nullptr});
  const auto declarations = registry.declarations.equal_range(flag.name());
  for (auto declaration = declarations.first;
       declaration != declarations.second; ++declaration) {
    if (declaration->second.type != flag.type_name()) {
      AbortMismatchedDeclaration(flag.name(), declaration->second.filename,
                                 declaration->second.type, flag);
    }
  }
}

void UnregisterFlag(FlagBase& flag) {
  Registry& registry = GetRegistry();
  const std::lock_guard<RegistryMutex> lock(registry.mutex);
  const auto entry = registry.entries.find(&flag);
  if (entry == registry.entries.end()) {
    return;
  }
  registry.entries.erase(entry);
  const auto found = registry.flags.find(flag.name());
  if (found != registry.flags.end() && found->second == &flag) {
    registry.flags.erase(found);
    registry.flag_help_formatters.erase(flag.name());
    registry.flag_displays.erase(flag.name());
    registry.process_flags.Unregister(flag.name(), flag.filename());
  }
}

namespace internal {

std::uint64_t FlagRegistration(const FlagBase* flag) noexcept {
  Registry& registry = GetRegistry();
  const std::lock_guard<RegistryMutex> lock(registry.mutex);
  const auto entry = registry.entries.find(flag);
  return entry == registry.entries.end() ? 0 : entry->second.registration;
}

RegisteredFlagLock::RegisteredFlagLock(const FlagBase* flag,
                                       std::uint64_t registration) {
  Registry& registry = GetRegistry();
  registry.mutex.lock();
  if (registration == 0) {
    // The flag was saved before its registration. Flags defined by
    // NGLOG_DEFINE are never destroyed, so the object is still valid.
    registered_ = true;
    return;
  }
  const auto entry = registry.entries.find(flag);
  registered_ = entry != registry.entries.end() &&
                entry->second.registration == registration;
}

RegisteredFlagLock::~RegisteredFlagLock() { GetRegistry().mutex.unlock(); }

RegisteredFlagLock::operator bool() const noexcept { return registered_; }

FlagDeclaration::FlagDeclaration(const char* name, const char* type,
                                 const char* filename) {
  Registry& registry = GetRegistry();
  const std::lock_guard<RegistryMutex> lock(registry.mutex);
  const auto definition = registry.flags.find(name);
  if (definition != registry.flags.end() &&
      std::strcmp(definition->second->type_name(), type) != 0) {
    AbortMismatchedDeclaration(name, filename, type, *definition->second);
  }
  registry.declarations.emplace(name,
                                FlagDeclarationEntry{this, type, filename});
}

FlagDeclaration::~FlagDeclaration() {
  Registry& registry = GetRegistry();
  const std::lock_guard<RegistryMutex> lock(registry.mutex);
  for (auto declaration = registry.declarations.begin();
       declaration != registry.declarations.end(); ++declaration) {
    if (declaration->second.declaration == this) {
      registry.declarations.erase(declaration);
      return;
    }
  }
}

std::uint64_t NextScopedFlagSequence() noexcept {
  static std::atomic<std::uint64_t> sequence{0};
  return ++sequence;
}

FlagRegistrar::FlagRegistrar(FlagBase& flag) : flag_(flag) {
  RegisterFlag(flag_);
}

FlagRegistrar::~FlagRegistrar() { UnregisterFlag(flag_); }

void FlagSpinLock::lock() noexcept {
  while (locked_.exchange(true, std::memory_order_acquire)) {
    std::this_thread::yield();
  }
}

void FlagSpinLock::unlock() noexcept {
  locked_.store(false, std::memory_order_release);
}

FlagValueStorage<std::string>::FlagValueStorage(const std::string& value)
    : storage_(value), emptiness_(value.empty() ? kEmpty : kNotEmpty) {}

std::string FlagValueStorage<std::string>::Get() const {
  return storage_.Get();
}

std::string FlagValueStorage<std::string>::GetDefault() const {
  return storage_.GetDefault();
}

bool FlagValueStorage<std::string>::ExplicitlySet() const {
  return storage_.ExplicitlySet();
}

void FlagValueStorage<std::string>::Restore(const std::string& value,
                                            bool explicitly_set) {
  storage_.Restore(value, explicitly_set,
                   [this](const std::string& stored) { Record(stored); });
}

void FlagValueStorage<std::string>::SetDefault(const std::string& value) {
  storage_.SetDefault(value,
                      [this](const std::string& stored) { Record(stored); });
}

bool FlagValueStorage<std::string>::Empty() const {
  const unsigned char emptiness = emptiness_.load(std::memory_order_relaxed);
  if (emptiness != kUnknown) {
    return emptiness == kEmpty;
  }
  bool empty = true;
  storage_.Observe([this, &empty](const std::string& stored) {
    Record(stored);
    empty = stored.empty();
  });
  return empty;
}

void FlagValueStorage<std::string>::Initialize() const {
  storage_.Initialize();
}

void FlagValueStorage<std::string>::Destroy() noexcept { storage_.Destroy(); }

void FlagValueStorage<std::string>::LockForFork() const noexcept {
  storage_.LockForFork();
}

void FlagValueStorage<std::string>::UnlockAfterFork() const noexcept {
  storage_.UnlockAfterFork();
}

void FlagValueStorage<std::string>::Record(
    const std::string& stored) const noexcept {
  emptiness_.store(stored.empty() ? kEmpty : kNotEmpty,
                   std::memory_order_relaxed);
}

bool FlagValueAccess::Empty(const Flag<std::string>& flag) {
  return flag.values_.Empty();
}

}  // namespace internal

FileMode::FileMode(std::uint32_t mode) noexcept : mode_(mode) {}

std::uint32_t FileMode::value() const noexcept { return mode_; }

HelpWriter::HelpWriter(Impl* impl) noexcept : impl_(impl) {}

const char* FlagBase::name() const noexcept { return name_; }

const char* FlagBase::filename() const noexcept { return filename_; }

const char* FlagBase::help() const noexcept { return help_; }

HelpCallback FlagBase::help_callback() const noexcept { return help_callback_; }

bool FlagBase::SetFromString(const std::string& value, std::string* error,
                             internal::FlagParseError* /*details*/) const {
  return SetFromString(value, error);
}

bool FlagBase::SetDefaultFromString(const std::string& value,
                                    std::string* error) const {
  return SetFromString(value, error);
}

void FlagBase::InitializeValues() const {}

bool FlagBase::ValueIsExplicitlySet() const { return true; }

bool FlagBase::RestoreFromString(const std::string& value,
                                 bool /*explicitly_set*/,
                                 std::string* error) const {
  return SetFromString(value, error);
}

void FlagBase::LockValuesForFork() const {}

void FlagBase::UnlockValuesAfterFork() const {}

FlagGroup::FlagGroup() = default;

FlagGroup::FlagGroup(const std::string& module, const std::string& name,
                     const std::shared_ptr<const void>& module_identity,
                     const std::shared_ptr<const void>& identity)
    : module_(module),
      name_(name),
      module_identity_(module_identity),
      identity_(identity) {}

ScopedFlags<>::ScopedFlags(ScopedFlags&& other) noexcept
    : values_(std::move(other.values_)) {}

ScopedFlags<>::ScopedFlags() {
  Registry& registry = GetRegistry();
  const std::lock_guard<RegistryMutex> lock(registry.mutex);
  values_.reserve(registry.flags.size());
  for (const auto& entry : registry.flags) {
    values_.emplace_back(
        entry.second, registry.entries.at(entry.second).registration,
        entry.second->CurrentValue(),
        internal::FlagRegistryAccess::ValueIsExplicitlySet(*entry.second));
  }
}

ScopedFlags<>::~ScopedFlags() {
  for (const auto& value : values_) {
    const internal::RegisteredFlagLock lock(std::get<0>(value),
                                            std::get<1>(value));
    if (!lock) {
      continue;
    }
    std::string error;
    if (!internal::FlagRegistryAccess::RestoreFromString(
            *std::get<0>(value), std::get<2>(value), std::get<3>(value),
            &error)) {
      AbortFlagError("could not restore command-line option",
                     *std::get<0>(value), error);
    }
  }
}

void GetAllFlags(std::vector<FlagInfo>* flags) {
  Registry& registry = GetRegistry();
  const std::lock_guard<RegistryMutex> lock(registry.mutex);
  GetAllFlagsLocked(flags);
}

FlagModule::FlagModule(const std::string& name, const std::string& header,
                       const std::string& environment_prefix)
    : name_(name), identity_(std::make_shared<int>(0)) {
  Registry& registry = GetRegistry();
  const std::lock_guard<RegistryMutex> lock(registry.mutex);
  if (!name_.empty() &&
      registry.modules
          .emplace(
              name_,
              Registry::ModuleInfo{header, environment_prefix, identity_, {}})
          .second) {
    registry.module_order.push_back(name_);
  } else if (!name_.empty()) {
    AbortDuplicateModule(name_);
  }
}

FlagModule::~FlagModule() {
  if (name_.empty()) {
    return;
  }
  Registry& registry = GetRegistry();
  const std::lock_guard<RegistryMutex> lock(registry.mutex);
  const auto module = registry.modules.find(name_);
  if (module == registry.modules.end() ||
      module->second.identity.get() != identity_.get()) {
    return;
  }
  registry.modules.erase(module);
  registry.module_order.erase(std::remove(registry.module_order.begin(),
                                          registry.module_order.end(), name_),
                              registry.module_order.end());
  if (registry.module_order.empty()) {
    // Releases the memory of a copy of the library that is being unloaded.
    registry.module_order.shrink_to_fit();
  }
  for (auto& entry : registry.entries) {
    if (entry.second.module_identity == identity_.get()) {
      entry.second.module.clear();
      entry.second.group.clear();
      entry.second.module_identity = nullptr;
    }
  }
}

FlagGroup FlagModule::AddGroup(const std::string& name) const {
  Registry& registry = GetRegistry();
  const std::lock_guard<RegistryMutex> lock(registry.mutex);
  const auto module = registry.modules.find(name_);
  if (name.empty() || name_.empty() || module == registry.modules.end() ||
      module->second.identity.get() != identity_.get()) {
    return FlagGroup();
  }
  for (const Registry::ModuleInfo::GroupInfo& group : module->second.groups) {
    if (group.name == name) {
      return FlagGroup(name_, name, identity_, group.identity);
    }
  }
  const auto group_identity = std::make_shared<int>(0);
  module->second.groups.push_back({name, group_identity});
  return FlagGroup(name_, name, identity_, group_identity);
}

bool FlagGroup::AddFlag(FlagBase& flag) const {
  Registry& registry = GetRegistry();
  const std::lock_guard<RegistryMutex> lock(registry.mutex);
  const auto registered_flag = registry.flags.find(flag.name());
  const auto module = registry.modules.find(module_);
  if (name_.empty() || module_.empty() ||
      registered_flag == registry.flags.end() ||
      registered_flag->second != &flag || module == registry.modules.end() ||
      module->second.identity.get() != module_identity_.get()) {
    return false;
  }
  const auto group =
      std::find_if(module->second.groups.begin(), module->second.groups.end(),
                   [this](const Registry::ModuleInfo::GroupInfo& candidate) {
                     return candidate.name == name_ &&
                            candidate.identity.get() == identity_.get();
                   });
  internal::FlagEntry& entry = registry.entries.at(&flag);
  if (group == module->second.groups.end() || !entry.module.empty()) {
    return false;
  }
  entry.module = module_;
  entry.group = name_;
  entry.module_identity = module_identity_.get();
  const auto environment_prefix = module->second.environment_prefix;
  if (!environment_prefix.empty()) {
    const std::string environment_name = environment_prefix + flag.name();
    const char* const environment_value = std::getenv(environment_name.c_str());
    if (environment_value != nullptr) {
      std::string error;
      flag.SetDefaultFromString(environment_value, &error);
    }
  }
  return true;
}

bool GetCommandLineOption(const char* name, std::string* value) {
  Registry& registry = GetRegistry();
  const std::lock_guard<RegistryMutex> lock(registry.mutex);
  FlagBase* const flag = FindFlagLocked(name);
  if (flag == nullptr) {
    return false;
  }
  *value = flag->CurrentValue();
  return true;
}

bool SetCommandLineOption(const char* name, const char* value) {
  return SetCommandLineOption(name, value, nullptr);
}

bool SetCommandLineOption(const char* name, const char* value,
                          std::string* error) {
  Registry& registry = GetRegistry();
  const std::lock_guard<RegistryMutex> lock(registry.mutex);
  FlagBase* const flag = FindFlagLocked(name);
  if (flag == nullptr) {
    if (error != nullptr) {
      *error =
          std::string("unknown command-line option ") + kOptionPrefix + name;
    }
    return false;
  }
  std::string parse_error;
  const bool result = flag->SetFromString(value, &parse_error);
  if (!result && error != nullptr) {
    // Quotes delimit empty values and values with surrounding spaces.
    *error = "invalid value \"" + std::string(value) + "\" for " +
             flag->type_name() + " flag";
    if (!parse_error.empty()) {
      *error += ", " + parse_error;
    }
  }
  return result;
}

void SetUsageMessage(const std::string& usage) {
  Registry& registry = GetRegistry();
  const std::lock_guard<RegistryMutex> lock(registry.mutex);
  registry.usage = usage;
  registry.usage_function = {};
}

void SetUsageMessage(const HelpMessageFunction& usage) {
  Registry& registry = GetRegistry();
  const std::lock_guard<RegistryMutex> lock(registry.mutex);
  registry.usage.clear();
  registry.usage_function = usage;
}

namespace internal {

void SetFlagDisplay(const FlagBase& flag, const char* type, const char* unit) {
  Registry& registry = GetRegistry();
  const std::lock_guard<RegistryMutex> lock(registry.mutex);
  const auto registered = registry.flags.find(flag.name());
  if (registered == registry.flags.end() || registered->second != &flag) {
    return;
  }
  registry.flag_displays[flag.name()] =
      FlagDisplay{type == nullptr ? "" : type, unit == nullptr ? "" : unit};
}

}  // namespace internal

bool SetFlagHelpFormatter(const char* name, const FlagHelpFunction& formatter) {
  Registry& registry = GetRegistry();
  const std::lock_guard<RegistryMutex> lock(registry.mutex);
  if (registry.flags.find(name) == registry.flags.end()) {
    return false;
  }
  if (formatter) {
    registry.flag_help_formatters[name] = formatter;
  } else {
    registry.flag_help_formatters.erase(name);
  }
  return true;
}

std::string ProgramUsage() {
  Registry& registry = GetRegistry();
  std::string usage;
  HelpMessageFunction usage_function;
  {
    const std::lock_guard<RegistryMutex> lock(registry.mutex);
    usage = registry.usage;
    usage_function = registry.usage_function;
  }
  if (usage_function) {
    usage.clear();
    internal::HelpWriterContext context{nullptr, internal::ColorMode::kNone,
                                        &usage, nullptr};
    internal::HelpWriterAccess::Invoke(usage_function, &context);
  }
  return usage;
}

void PrintCommandLineFlags() { PrintHelp(); }

std::uint32_t ParseCommandLineFlags(int* argc, char*** argv,
                                    FlagParsingMode mode,
                                    UnknownFlagMode unknown_mode) {
  Registry& registry = GetRegistry();
  std::unique_lock<RegistryMutex> lock(registry.mutex);
  std::uint32_t parsed_flags = 0;
  int output = 1;
  bool end_of_flags = false;
  for (int input = 1; input < *argc; ++input) {
    char* const argument = (*argv)[input];
    const bool separator =
        !end_of_flags && std::strcmp(argument, kEndOfOptions) == 0;
    if (separator) {
      end_of_flags = true;
      // The separator is consumed like a flag, unless another parser handles
      // the remaining options and needs to know where they end.
      if (mode == FlagParsingMode::kRemoveFlags &&
          unknown_mode == UnknownFlagMode::kIgnore) {
        (*argv)[output++] = argument;
      }
      continue;
    }
    if (end_of_flags ||
        std::strncmp(argument, kOptionPrefix, kOptionPrefixLength) != 0) {
      // A single dash followed by a name is an option this parser does not
      // accept, whereas a lone dash conventionally denotes standard input.
      const bool single_dash = argument[0] == '-' && argument[1] != '\0';
      if (!end_of_flags && single_dash &&
          unknown_mode == UnknownFlagMode::kError) {
        const std::string option(argument + 1);
        const std::string name = option.substr(0, option.find('='));
        const std::string suggestion =
            IsLongOptionLocked(name) ? name : FindClosestOptionLocked(name);
        lock.unlock();
        FatalUnknownFlag("-", name, suggestion);
      }
      if (mode == FlagParsingMode::kRemoveFlags) {
        (*argv)[output++] = argument;
      }
      continue;
    }

    std::string name(argument + kOptionPrefixLength);
    std::string value;
    const std::size_t equals = name.find('=');
    if (equals != std::string::npos) {
      value = name.substr(equals + 1);
      name.erase(equals);
    }

    if (name == kBuiltinHelpName) {
      if (equals != std::string::npos) {
        lock.unlock();
        ExitFlagError(std::string("command-line option ") + kOptionPrefix +
                      kBuiltinHelpName + " does not accept a value");
      }
      HelpSnapshot snapshot;
      GetHelpSnapshotLocked(&snapshot);
      lock.unlock();
      PrintHelpSnapshot(snapshot);
      std::exit(0);
    }

    bool negated = false;
    FlagBase* flag = FindFlagLocked(name);
    if (flag == nullptr &&
        name.compare(0, kNegationPrefixLength, kNegationPrefix) == 0) {
      FlagBase* const candidate =
          FindFlagLocked(name.substr(kNegationPrefixLength));
      if (candidate != nullptr &&
          std::strcmp(candidate->type_name(), FlagTraits<bool>::TypeName()) !=
              0) {
        lock.unlock();
        ExitFlagError(std::string("command-line option ") + kOptionPrefix +
                      name + " cannot be negated because it is not boolean");
      }
      flag = candidate;
      negated = flag != nullptr;
    }
    if (flag == nullptr) {
      if (unknown_mode == UnknownFlagMode::kIgnore) {
        if (mode == FlagParsingMode::kRemoveFlags) {
          (*argv)[output++] = argument;
        }
        continue;
      }
      const std::string suggestion = FindClosestOptionLocked(name);
      lock.unlock();
      FatalUnknownFlag(kOptionPrefix, name, suggestion);
    }

    if (negated) {
      if (equals != std::string::npos) {
        lock.unlock();
        ExitFlagError(std::string("command-line option ") + kOptionPrefix +
                      name + " does not accept a value when negated");
      }
      value = "false";
    } else if (equals == std::string::npos &&
               std::strcmp(flag->type_name(), FlagTraits<bool>::TypeName()) ==
                   0) {
      value = "true";
    } else if (equals == std::string::npos) {
      if (input + 1 >= *argc) {
        lock.unlock();
        ExitFlagError(std::string("command-line option ") + kOptionPrefix +
                      name + " is missing its value");
      }
      value = (*argv)[++input];
    }

    std::string error;
    internal::FlagParseError details;
    if (!flag->SetFromString(value, &error, &details)) {
      const std::string flag_name = flag->name();
      const std::string type_name = flag->type_name();
      lock.unlock();
      ExitFlagError("could not set command-line option", flag_name, type_name,
                    details, error);
    }
    ++parsed_flags;
  }
  if (mode == FlagParsingMode::kRemoveFlags) {
    *argc = output;
    (*argv)[output] = nullptr;
  }
  return parsed_flags;
}

}  // namespace nglog
