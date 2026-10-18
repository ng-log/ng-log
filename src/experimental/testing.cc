// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Entry point of the tests of the experimental logging functions.

#include "testing.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdio>

#include "ng-log/experimental/attributes.h"
#include "ng-log/experimental/signal_safe_log.h"
#include "ng-log/flags.h"
#include "ng-log/logging.h"

namespace ngx_testing {

std::array<const void*, kPredefinedAttributes>
PredefinedAttributesOfAnotherTranslationUnit() {
  return {&ngx::Info,        &ngx::Warning,      &ngx::Error,
          &ngx::Fatal,       &ngx::FatalInDebug, &ngx::Errno,
          &ngx::Debug,       &ngx::Occurrence,   &ngx::Syslog,
          &ngx::SkipLogFile, &ngx::SignalSafe};
}

}  // namespace ngx_testing

int main(int argc, char** argv) {
  nglog::SetFlag(FLAGS_colorlogtostderr, false);
  std::setvbuf(stderr, nullptr, _IONBF, 0);

  testing::InitGoogleTest(&argc, argv);
  nglog::InitializeLogging(argv[0]);

  return RUN_ALL_TESTS();
}
