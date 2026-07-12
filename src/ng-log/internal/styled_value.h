// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_STYLED_VALUE_H
#define NGLOG_INTERNAL_STYLED_VALUE_H

#include "ng-log/internal/style_markers.h"

namespace nglog {
namespace internal {

template <typename T>
struct StyledValue {
  TextAttributes attributes;
  const T& value;
};

// Styled is a value wrapper for stream-based callers. LogMessage::AppendText
// provides the corresponding formatter-neutral entry point for callers that
// already have formatted text, including future std::format adapters.
template <typename T>
inline StyledValue<T> Styled(TextAttributes attributes, const T& value) {
  return StyledValue<T>{attributes, value};
}

template <typename T>
inline StyledValue<T> Styled(ColorSpec color, const T& value) {
  return Styled(PushStyle(color).attributes, value);
}

template <typename T>
inline StyledValue<T> Styled(Hyperlink hyperlink, const T& value) {
  return Styled(PushStyle(hyperlink).attributes, value);
}

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_STYLED_VALUE_H
