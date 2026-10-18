// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_TEXT_ATTRIBUTES_H
#define NGLOG_INTERNAL_TEXT_ATTRIBUTES_H

// IWYU pragma: private, include "ng-log/logging.h"
// IWYU pragma: friend "ng-log/internal/.*"

#include "ng-log/internal/color_spec.h"
#include "ng-log/internal/hyperlink.h"

namespace nglog {
namespace internal {

struct TextAttributes {
  ColorSpec color;
  Hyperlink hyperlink;
};

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_TEXT_ATTRIBUTES_H
