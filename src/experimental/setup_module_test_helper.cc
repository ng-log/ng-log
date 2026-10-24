// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "setup_module_test_helper.h"

#include "ng-log/experimental/attributes.h"
#include "ng-log/experimental/log.h"

namespace ngx_testing {

bool IsVerboseInSetupTestModule(int level) {
  return nglog::experimental::IsEnabled(nglog::experimental::Verbose{level});
}

}  // namespace ngx_testing
