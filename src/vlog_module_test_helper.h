// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Verbose logging from a module of its own. Overriding the verbosity of this
// module leaves the verbose logging of every other source file unchanged, so
// tests that override it can run in the same process as other tests.

#ifndef NGLOG_SRC_VLOG_MODULE_TEST_HELPER_H_
#define NGLOG_SRC_VLOG_MODULE_TEST_HELPER_H_

namespace nglog {
namespace testing_internal {

//! Module name under which this helper logs verbose messages.
constexpr char kVlogTestModule[] = "vlog_module_test_helper";

//! Reports whether verbose logging at `level` is enabled in the module.
bool IsVerboseInTestModule(int level);

}  // namespace testing_internal
}  // namespace nglog

#endif  // NGLOG_SRC_VLOG_MODULE_TEST_HELPER_H_
