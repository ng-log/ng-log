// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Declares application flags in the ways the documentation allows. The
// program is compiled with pedantic warnings treated as errors, so that the
// macros remain standard C++.

#include <ng-log/flags.h>

#include <cstdint>

NGLOG_DECLARE(std::int32_t, retries);
// Repeating a declaration is allowed.
NGLOG_DECLARE(std::int32_t, retries);

namespace app {
NGLOG_DECLARE(bool, dry_run);
}  // namespace app

NGLOG_DEFINE(std::int32_t, retries, 3,
             "Retry failed requests this many times.");

namespace app {
NGLOG_DEFINE(bool, dry_run, false,
             "Print the actions instead of running them.");
}  // namespace app

int main() {
  NGLOG_DECLARE(std::int32_t, retries);
  return nglog::GetFlag(FLAGS_retries) == 3 &&
                 !nglog::GetFlag(app::FLAGS_dry_run)
             ? 0
             : 1;
}
