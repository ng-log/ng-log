// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause

#include "ng-log/experimental/logging.h"

namespace ngx = nglog::experimental;

int main(int /*argc*/, char** argv) {
  nglog::InitializeLogging(argv[0]);
  ngx::Log(ngx::Info, "{}", argv[0]);
}
