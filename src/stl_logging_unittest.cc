// SPDX-FileCopyrightText: 2003-2023 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause

#include "ng-log/stl_logging.h"

#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "config.h"
#include "ng-log/logging.h"

namespace {

//! Number of elements printed before the output is truncated.
constexpr int kMaxPrintedElements = 100;

template <class T>
std::string ToString(const T& value) {
  std::ostringstream stream;
  stream << value;
  return stream.str();
}

}  // namespace

TEST(STLLogging, Sequence) {
  const std::vector<int> v{10, 20, 30};
  EXPECT_EQ(ToString(v), "10 20 30");
  const std::vector<int> copied_v(v);
  CHECK_EQ(v, copied_v);  // This must compile.
}

TEST(STLLogging, SortedPairAssociativeContainer) {
  const std::map<int, std::string> m{
      {20, "twenty"}, {10, "ten"}, {30, "thirty"}};
  EXPECT_EQ(ToString(m), "(10, ten) (20, twenty) (30, thirty)");
  const std::map<int, std::string> copied_m(m);
  CHECK_EQ(m, copied_m);  // This must compile.
}

TEST(STLLogging, LongSequenceIsTruncated) {
  std::vector<int> v;
  std::string expected;
  for (int i = 0; i < kMaxPrintedElements; ++i) {
    v.push_back(i);
    if (i > 0) {
      expected += ' ';
    }
    expected += std::to_string(i);
  }
  v.push_back(kMaxPrintedElements);
  expected += " ...";
  EXPECT_EQ(ToString(v), expected);
}

TEST(STLLogging, SortedPairAssociativeContainerWithCustomComparison) {
  const std::map<int, std::string, std::greater<>> m{
      {20, "twenty"}, {10, "ten"}, {30, "thirty"}};
  EXPECT_EQ(ToString(m), "(30, thirty) (20, twenty) (10, ten)");
  const std::map<int, std::string, std::greater<>> copied_m(m);
  CHECK_EQ(m, copied_m);  // This must compile.
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
