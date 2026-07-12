// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "ng-log/internal/hyperlink.h"

namespace nglog {
namespace internal {

Hyperlink::Hyperlink() noexcept = default;
Hyperlink::Hyperlink(std::nullptr_t) noexcept : uri_(nullptr) {}
Hyperlink::Hyperlink(const char* uri) noexcept : uri_(uri) {}

}  // namespace internal
}  // namespace nglog
