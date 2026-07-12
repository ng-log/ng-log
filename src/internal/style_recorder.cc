// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "style_recorder.h"

#include <cstring>

namespace nglog {
namespace internal {

void StyleRecorder::Reset() noexcept {
  span_count_ = 0;
  active_count_ = 0;
  uri_size_ = 0;
  disabled_ = false;
}

void StyleRecorder::Disable() noexcept {
  span_count_ = 0;
  active_count_ = 0;
  uri_size_ = 0;
  disabled_ = true;
}

bool StyleRecorder::StoreAttributes(const TextAttributes& attributes,
                                    TextAttributes* stored) noexcept {
  *stored = attributes;
  const Hyperlink& hyperlink = attributes.hyperlink;
  if (hyperlink.uri() == nullptr) {
    stored->hyperlink = Hyperlink();
    return true;
  }

  const std::size_t uri_length = std::strlen(hyperlink.uri());
  constexpr std::size_t kNullTerminatorLength = 1;
  if (uri_length > kMaxUriBytes - uri_size_ - kNullTerminatorLength) {
    return false;
  }

  char* const uri = uri_storage_ + uri_size_;
  std::memcpy(uri, hyperlink.uri(), uri_length + kNullTerminatorLength);
  uri_size_ += uri_length + kNullTerminatorLength;
  stored->hyperlink = Hyperlink(uri);
  return true;
}

bool StyleRecorder::AddSpan(std::size_t begin, std::size_t end,
                            const TextAttributes& attributes) noexcept {
  if (end <= begin) {
    return true;
  }
  if (span_count_ >= kMaxSpans) {
    return false;
  }
  spans_[span_count_++] = Span{begin, end, attributes};
  return true;
}

void StyleRecorder::Push(std::size_t offset,
                         const TextAttributes& attributes) noexcept {
  if (disabled_) {
    return;
  }
  if (active_count_ >= kMaxSpans) {
    Disable();
    return;
  }

  if (active_count_ > 0) {
    ActiveStyle& current = active_[active_count_ - 1];
    if (!AddSpan(current.begin, offset, current.attributes)) {
      Disable();
      return;
    }
    current.begin = offset;
  }

  TextAttributes stored;
  if (!StoreAttributes(attributes, &stored)) {
    Disable();
    return;
  }
  active_[active_count_++] = ActiveStyle{offset, stored};
}

void StyleRecorder::Pop(std::size_t offset) noexcept {
  if (disabled_ || active_count_ == 0) {
    return;
  }

  const ActiveStyle& current = active_[active_count_ - 1];
  if (!AddSpan(current.begin, offset, current.attributes)) {
    Disable();
    return;
  }
  --active_count_;
  if (active_count_ > 0) {
    active_[active_count_ - 1].begin = offset;
  }
}

void StyleRecorder::Close(std::size_t offset) noexcept {
  if (disabled_) {
    return;
  }

  while (active_count_ != 0) {
    const ActiveStyle& current = active_[active_count_ - 1];
    if (!AddSpan(current.begin, offset, current.attributes)) {
      Disable();
      return;
    }
    --active_count_;
  }
}

}  // namespace internal
}  // namespace nglog
