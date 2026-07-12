// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_STYLE_MARKERS_H
#define NGLOG_INTERNAL_STYLE_MARKERS_H

#include "ng-log/internal/text_attributes.h"

namespace nglog {
namespace internal {

// PushStyle and PopStyle are stream markers. They are recognized by a
// LogMessage stream and ignored by ordinary streams. Markers nest, and a pop
// without a matching push is ignored. Attributes replace the current style
// for the span instead of overlaying it.
struct StylePush {
  TextAttributes attributes;
};

struct StylePop {};

inline StylePush PushStyle(TextAttributes attributes) {
  return StylePush{attributes};
}

inline StylePush PushStyle(ColorSpec color) {
  return PushStyle(TextAttributes{color, Hyperlink()});
}

inline StylePush PushStyle(Hyperlink hyperlink) {
  return PushStyle(TextAttributes{ColorSpec{}, hyperlink});
}

inline StylePop PopStyle() { return StylePop{}; }

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_STYLE_MARKERS_H
