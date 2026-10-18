// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Implements the attribute that marks a message as a failed check.

#include "ng-log/experimental/ensure.h"

#include "ng-log/experimental/attributes.h"
#include "ng-log/log_severity.h"

namespace nglog::experimental::internal {

void EnsureFailure::Configure(Context& context) const {
  context.set_severity(NGLOG_FATAL);
}

void EnsureFailure::Prefix(Message& message) const {
  switch (kind) {
    case EnsureFailureKind::kCondition:
      message.Append(kCheckFailedPrefix);
      break;
    case EnsureFailureKind::kComparison:
      message.Append(kCheckFailedPrefix);
      message.Append(description);
      message.Append(" ");
      break;
    case EnsureFailureKind::kStringComparison:
      message.Append(description);
      break;
    case EnsureFailureKind::kSeparatedStringComparison:
      message.Append(description);
      message.Append(" ");
      break;
  }
}

}  // namespace nglog::experimental::internal
