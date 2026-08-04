// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_HYPERLINK_H
#define NGLOG_INTERNAL_HYPERLINK_H

#include <cstddef>
#include <cstring>

#include "ng-log/export.h"

namespace nglog {
namespace internal {

class NGLOG_EXPORT Hyperlink {
 public:
  Hyperlink() noexcept;
  explicit Hyperlink(std::nullptr_t) noexcept;
  explicit Hyperlink(const char* uri) noexcept;

  const char* uri() const { return uri_; }

  // Formatter must provide AppendString(const char*, std::size_t). The body
  // is called exactly once. A null URI only calls the body.
  template <typename Formatter, typename Body>
  inline void Wrap(Formatter& formatter, Body&& body) const {
    if (uri_ == nullptr) {
      body();
      return;
    }
    constexpr char kPrefix[] = "\033]8;;";
    constexpr char kSeparator[] = "\033\\";
    constexpr char kSuffix[] = "\033]8;;\033\\";
    formatter.AppendString(kPrefix, sizeof(kPrefix) - 1);
    formatter.AppendString(uri_, std::strlen(uri_));
    formatter.AppendString(kSeparator, sizeof(kSeparator) - 1);
    body();
    formatter.AppendString(kSuffix, sizeof(kSuffix) - 1);
  }

 private:
  const char* uri_ = nullptr;
};

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_HYPERLINK_H
