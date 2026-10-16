// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#define third_party_export_override
#define NGLOG_EXPORT_FLAG third_party_export_override
#include "ng-log/flags.h"

NGLOG_DECLARE(std::int32_t, external_test_flag);
NGLOG_DEFINE(std::int32_t, external_test_flag, 7,
             "A flag defined by a consuming application.");

#undef NGLOG_EXPORT_FLAG
#undef third_party_export_override

TEST(ExternalFlags, CanBeDefinedByAConsumer) {
  EXPECT_EQ(nglog::GetFlag(FLAGS_external_test_flag), 7);
}

TEST(ExternalFlags, CanAccessLibraryFlagsThroughTypedObject) {
  const bool original = nglog::GetFlag(FLAGS_logtostderr);
  nglog::SetFlag(FLAGS_logtostderr, !original);
  EXPECT_EQ(!original, nglog::GetFlag(FLAGS_logtostderr));
  nglog::SetFlag(FLAGS_logtostderr, original);
}

TEST(ExternalFlags, SetFlagAcceptsConstructibleValue) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_external_test_flag, 19);
  EXPECT_EQ(nglog::GetFlag(FLAGS_external_test_flag), 19);
}

TEST(ExternalFlags, CanAssignModuleMetadata) {
  const nglog::FlagModule module("external-test", "Options provided by",
                                 "EXTTEST_");
  const auto group = module.AddGroup("External options");
  ASSERT_TRUE(group.AddFlag(FLAGS_external_test_flag));

  std::vector<nglog::FlagInfo> flags;
  nglog::GetAllFlags(&flags);
  const auto flag =
      std::find_if(flags.begin(), flags.end(), [](const nglog::FlagInfo& info) {
        return info.name == "external_test_flag";
      });
  ASSERT_NE(flag, flags.end());
  EXPECT_THAT(flag->module, testing::StrEq("external-test"));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
