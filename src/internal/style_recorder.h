// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_STYLE_RECORDER_H
#define NGLOG_INTERNAL_STYLE_RECORDER_H

#include <cstddef>

#include "ng-log/export.h"
#include "ng-log/internal/text_attributes.h"

namespace nglog {
namespace internal {

class NGLOG_NO_EXPORT StyleRecorder {
 public:
  static constexpr std::size_t kMaxSpans = 256;
  static constexpr std::size_t kMaxUriBytes = 8192;

  struct Span {
    std::size_t begin;
    std::size_t end;
    TextAttributes attributes;
  };

  StyleRecorder() noexcept { Reset(); }

  void Reset() noexcept;
  void Push(std::size_t offset, const TextAttributes& attributes) noexcept;
  void Pop(std::size_t offset) noexcept;
  void Close(std::size_t offset) noexcept;

  std::size_t size() const noexcept { return span_count_; }
  const Span& span(std::size_t index) const noexcept { return spans_[index]; }

 private:
  struct ActiveStyle {
    std::size_t begin;
    TextAttributes attributes;
  };

  bool StoreAttributes(const TextAttributes& attributes,
                       TextAttributes* stored) noexcept;
  bool AddSpan(std::size_t begin, std::size_t end,
               const TextAttributes& attributes) noexcept;
  void Disable() noexcept;

  Span spans_[kMaxSpans];
  ActiveStyle active_[kMaxSpans];
  char uri_storage_[kMaxUriBytes];
  std::size_t span_count_;
  std::size_t active_count_;
  std::size_t uri_size_;
  bool disabled_;
};

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_STYLE_RECORDER_H
