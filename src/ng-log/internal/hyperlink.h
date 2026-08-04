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

namespace hyperlink_detail {

template <typename Formatter>
auto AppendString(Formatter& formatter, const char* text, std::size_t length,
                  int)
    -> decltype(formatter.AppendString(text, length), void()) {
  formatter.AppendString(text, length);
}

template <typename Formatter>
auto AppendString(Formatter& formatter, const char* text, std::size_t, long)
    -> decltype(formatter.AppendString(text), void()) {
  formatter.AppendString(text);
}

}  // namespace hyperlink_detail

class NGLOG_EXPORT Hyperlink {
 public:
  Hyperlink() noexcept;
  explicit Hyperlink(std::nullptr_t) noexcept;
  explicit Hyperlink(const char* uri) noexcept;

  const char* uri() const { return uri_; }

  // Formatter must provide AppendString(const char*) or
  // AppendString(const char*, std::size_t). The body is called exactly once.
  template <typename Formatter, typename Body>
  inline void Wrap(Formatter& formatter, Body&& body) const {
    if (uri_ == nullptr) {
      body();
      return;
    }
    constexpr char kPrefix[] = "\033]8;;";
    constexpr char kSeparator[] = "\033\\";
    constexpr char kSuffix[] = "\033]8;;\033\\";
    hyperlink_detail::AppendString(formatter, kPrefix, sizeof(kPrefix) - 1, 0);
    hyperlink_detail::AppendString(formatter, uri_, std::strlen(uri_), 0);
    hyperlink_detail::AppendString(formatter, kSeparator,
                                   sizeof(kSeparator) - 1, 0);
    body();
    hyperlink_detail::AppendString(formatter, kSuffix, sizeof(kSuffix) - 1, 0);
  }

 private:
  const char* uri_ = nullptr;
};

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_HYPERLINK_H
