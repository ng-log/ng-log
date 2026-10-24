// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Verbose logging from a module of its own. Overriding the verbosity of this
// module leaves the verbose logging of every other source file unchanged.

#ifndef NGLOG_SRC_EXPERIMENTAL_SETUP_MODULE_TEST_HELPER_H
#define NGLOG_SRC_EXPERIMENTAL_SETUP_MODULE_TEST_HELPER_H

#include <string_view>

namespace ngx_testing {

// Module name under which the helper logs verbose messages.
inline constexpr std::string_view kSetupTestModule = "setup_module_test_helper";

// Whether Verbose messages of the level are logged in the module.
bool IsVerboseInSetupTestModule(int level);

}  // namespace ngx_testing

#endif  // NGLOG_SRC_EXPERIMENTAL_SETUP_MODULE_TEST_HELPER_H
