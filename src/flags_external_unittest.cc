// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#define third_party_export_override
#define NGLOG_EXPORT_FLAG third_party_export_override
#include "ng-log/flags.h"

namespace {

constexpr std::int32_t kExternalDefault = 7;
constexpr std::int32_t kExternalUpdated = 19;

}  // namespace

NGLOG_DECLARE(std::int32_t, external_test_flag);
NGLOG_DEFINE(std::int32_t, external_test_flag, kExternalDefault,
             "A flag defined by a consuming application.");

#undef NGLOG_EXPORT_FLAG
#undef third_party_export_override

TEST(ExternalFlags, CanBeDefinedByAConsumer) {
  EXPECT_EQ(kExternalDefault, nglog::GetFlag(FLAGS_external_test_flag));
}

TEST(ExternalFlags, CanAccessLibraryFlagsThroughTypedObject) {
  nglog::ScopedFlags<> save;
  const bool original = nglog::GetFlag(FLAGS_logtostderr);
  nglog::SetFlag(FLAGS_logtostderr, !original);
  EXPECT_EQ(!original, nglog::GetFlag(FLAGS_logtostderr));
}

TEST(ExternalFlags, SetFlagAcceptsConstructibleValue) {
  nglog::ScopedFlags<> save;
  nglog::SetFlag(FLAGS_external_test_flag, kExternalUpdated);
  EXPECT_EQ(kExternalUpdated, nglog::GetFlag(FLAGS_external_test_flag));
}

TEST(ExternalFlags, CanAssignModuleMetadata) {
  const nglog::FlagModule module("external-test", "Options provided by",
                                 "EXTTEST_");
  const auto group = module.AddGroup("External options");
  ASSERT_TRUE(group.AddFlag(FLAGS_external_test_flag));

  std::vector<nglog::FlagInfo> flags;
  nglog::GetAllFlags(&flags);
  EXPECT_THAT(flags,
              testing::Contains(testing::AllOf(
                  testing::Field(&nglog::FlagInfo::name, "external_test_flag"),
                  testing::Field(&nglog::FlagInfo::module, "external-test"))));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
