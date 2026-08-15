// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Uses a flag from a static initializer that runs before the flag is
// registered, and from a static destructor that runs after the flag is
// unregistered. The order follows from the language alone: within one
// translation unit, variables are initialized in the order of their
// definitions and destroyed in reverse order, and the flag object itself is
// constant initialized before any dynamic initialization.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "ng-log/flags.h"

NGLOG_DECLARE(std::int32_t, static_initialization_flag);

namespace {

constexpr char kFlagName[] = "static_initialization_flag";
constexpr std::int32_t kDefaultValue = 5;
constexpr std::int32_t kUpdatedValue = 7;
constexpr std::int32_t kVerbosity = 2;

bool g_flag_was_unregistered = false;
bool g_flag_had_its_default = false;
bool g_flags_were_updated = false;

bool IsRegistered() {
  std::string value;
  return nglog::GetCommandLineOption(kFlagName, &value);
}

struct EarlyFlagUser {
  EarlyFlagUser() {
    g_flag_was_unregistered = !IsRegistered();
    g_flag_had_its_default =
        nglog::GetFlag(FLAGS_static_initialization_flag) == kDefaultValue;
    nglog::SetFlag(FLAGS_static_initialization_flag, kUpdatedValue);
    // The built-in flags are constant initialized as well, whether or not
    // their translation unit was initialized yet.
    nglog::SetFlag(FLAGS_v, kVerbosity);
    g_flags_were_updated =
        nglog::GetFlag(FLAGS_static_initialization_flag) == kUpdatedValue &&
        nglog::GetFlag(FLAGS_v) == kVerbosity;
  }

  ~EarlyFlagUser() {
    if (IsRegistered()) {
      std::fputs("the flag was still registered in the static destructor\n",
                 stderr);
      std::_Exit(EXIT_FAILURE);
    }
    if (nglog::GetFlag(FLAGS_static_initialization_flag) != kUpdatedValue ||
        nglog::GetFlag(FLAGS_v) != kVerbosity) {
      std::fputs("the flags were not usable in the static destructor\n",
                 stderr);
      std::_Exit(EXIT_FAILURE);
    }
  }
};

// Defined before the flag, so that it is constructed before the flag is
// registered and destroyed after the flag is unregistered.
EarlyFlagUser early_flag_user;

}  // namespace

NGLOG_DEFINE(std::int32_t, static_initialization_flag, kDefaultValue,
             "Flag used during static initialization and termination.");

int main() {
  if (!g_flag_was_unregistered || !g_flag_had_its_default ||
      !g_flags_were_updated) {
    std::fputs("the flags were not usable in the static initializer\n", stderr);
    return EXIT_FAILURE;
  }
  // The value set before the registration survives it.
  return IsRegistered() && nglog::GetFlag(FLAGS_static_initialization_flag) ==
                               kUpdatedValue
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
