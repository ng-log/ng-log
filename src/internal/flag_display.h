// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_FLAG_DISPLAY_H
#define NGLOG_INTERNAL_FLAG_DISPLAY_H

namespace nglog {

class FlagBase;

namespace internal {

/**
 * Overrides how the generated help presents a registered flag.
 *
 * @param flag The registered flag.
 * @param type The type shown instead of the flag's type name, or `nullptr` to
 *        keep the type name.
 * @param unit The unit appended to the default value, or `nullptr` for none.
 */
void SetFlagDisplay(const FlagBase& flag, const char* type, const char* unit);

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_FLAG_DISPLAY_H
