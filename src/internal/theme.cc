// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "theme.h"

namespace nglog {
namespace internal {

const Theme& DefaultTheme() {
  static constexpr auto theme =
      Theme{}
          .Set(Role::kLogWarning,
               ColorSpec{Color::kYellow, TextStyle::kNone, Color::kDefault})
          .Set(Role::kLogError,
               ColorSpec{Color::kRed, TextStyle::kNone, Color::kDefault})
          .Set(Role::kLogFatal,
               ColorSpec{Color::kRed, TextStyle::kBold, Color::kDefault})
          .Set(Role::kStackAddress,
               ColorSpec{Color::kGray, TextStyle::kNone, Color::kDefault})
          .Set(Role::kStackFile,
               ColorSpec{Color::kCyan, TextStyle::kNone, Color::kDefault})
          .Set(Role::kStackFunction,
               ColorSpec{Color::kGreen, TextStyle::kNone, Color::kDefault})
          .Set(Role::kStackUnresolved,
               ColorSpec{Color::kGray, TextStyle::kItalic, Color::kDefault})
          .Set(Role::kMetaIdentifier,
               ColorSpec{Color::kMagenta, TextStyle::kNone, Color::kDefault})
          .Set(Role::kMetaThreadName,
               ColorSpec{Color::kCyan, TextStyle::kNone, Color::kDefault})
          .Set(Role::kShellCommand,
               ColorSpec{Color::kBlue, TextStyle::kNone, Color::kDefault})
          .Set(Role::kSignalName,
               ColorSpec{Color::kRed, TextStyle::kBold, Color::kDefault})
          .Set(Role::kLogThreadId,
               ColorSpec{Color::kMagenta, TextStyle::kNone, Color::kDefault})
          .Set(Role::kLogFileLine,
               ColorSpec{Color::kCyan, TextStyle::kNone, Color::kDefault})
          .Set(Role::kLogBracket,
               ColorSpec{Color::kGray, TextStyle::kNone, Color::kDefault})
          .Set(Role::kErrnoMessage,
               ColorSpec{Color::kDefault, TextStyle::kDim, Color::kDefault})
          .Set(Role::kErrnoCode,
               ColorSpec{Color::kCyan, TextStyle::kBold, Color::kDefault});

  return theme;
}

}  // namespace internal
}  // namespace nglog
