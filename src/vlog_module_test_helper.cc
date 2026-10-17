// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause

#include "vlog_module_test_helper.h"

#include "ng-log/vlog_is_on.h"

namespace nglog {
namespace testing_internal {

bool IsVerboseInTestModule(int level) { return VLOG_IS_ON(level); }

}  // namespace testing_internal
}  // namespace nglog
