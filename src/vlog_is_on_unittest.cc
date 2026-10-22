// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause

#include "ng-log/vlog_is_on.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "ng-log/flags.h"
#include "vlog_module_test_helper.h"

using nglog::testing_internal::IsVerboseInTestModule;
using nglog::testing_internal::kVlogTestModule;

// Only GCC and compatible compilers support module levels in VLOG_IS_ON.
#if defined(GTEST_HAS_DEATH_TEST) && defined(__GNUC__)
namespace {

constexpr int kWildcardLevel = 1;
constexpr int kModuleLevel = 3;

}  // namespace

// Each child starts with the verbose logging state untouched, since nothing
// in this program evaluates a VLOG condition before the tests run.

TEST(SetVLOGLevel, OverridesVModuleBeforeFirstUse) {
  EXPECT_EXIT(
      {
        nglog::SetFlag(FLAGS_vmodule, std::string{kVlogTestModule} + "=" +
                                          std::to_string(kWildcardLevel));
        const int previous = nglog::SetVLOGLevel(kVlogTestModule, kModuleLevel);
        std::exit(previous == kWildcardLevel &&
                          IsVerboseInTestModule(kModuleLevel)
                      ? EXIT_SUCCESS
                      : EXIT_FAILURE);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST(SetVLOGLevel, OverridesMatchingWildcard) {
  EXPECT_EXIT(
      {
        nglog::SetFlag(FLAGS_vmodule,
                       "vlog_module_test_*=" + std::to_string(kWildcardLevel));
        // Parses --vmodule without binding the call site in the test module.
        static_cast<void>(VLOG_IS_ON(0));
        const int previous = nglog::SetVLOGLevel(kVlogTestModule, kModuleLevel);
        std::exit(previous == kWildcardLevel &&
                          IsVerboseInTestModule(kModuleLevel)
                      ? EXIT_SUCCESS
                      : EXIT_FAILURE);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
}
#endif

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
