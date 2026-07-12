// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_THEME_H
#define NGLOG_INTERNAL_THEME_H

#include <cstddef>
#include <utility>

#include "ng-log/export.h"
#include "ng-log/internal/color_spec.h"

namespace nglog {
namespace internal {

enum class Role {
  kLogInfo,
  kLogWarning,
  kLogError,
  kLogFatal,
  kStackAddress,
  kStackFile,
  kStackFunction,
  kStackUnresolved,
  kMetaIdentifier,
  kMetaThreadName,
  kShellCommand,
  kSignalName,
  kLogThreadId,
  kLogFileLine,
  kLogBracket,
  kErrnoMessage,
  kErrnoCode,
  kNumRoles
};

class NGLOG_NO_EXPORT Theme {
 public:
  constexpr ColorSpec Get(Role role) const noexcept {
    return specs_[static_cast<std::size_t>(role)];
  }

  constexpr Theme& Set(Role role, ColorSpec spec) & noexcept {
    specs_[static_cast<std::size_t>(role)] = spec;
    return *this;
  }

  constexpr Theme&& Set(Role role, ColorSpec spec) && noexcept {
    return std::move(Set(role, spec));
  }

 private:
  ColorSpec specs_[static_cast<std::size_t>(Role::kNumRoles)];
};

NGLOG_NO_EXPORT const Theme& DefaultTheme();

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_THEME_H
