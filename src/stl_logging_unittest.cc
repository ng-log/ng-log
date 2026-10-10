// SPDX-FileCopyrightText: 2003-2023 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause

#include "ng-log/stl_logging.h"

#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <ostream>
#include <string>
#include <vector>

#include "config.h"
#include "ng-log/logging.h"

using namespace std;

struct user_hash {
  size_t operator()(int x) const { return static_cast<size_t>(x); }
};

TEST(STLLogging, Basic) {
  {
    // Test a sequence.
    vector<int> v;
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);
    ostringstream ss;
    ss << v;
    EXPECT_EQ(ss.str(), "10 20 30");
    vector<int> copied_v(v);
    CHECK_EQ(v, copied_v);  // This must compile.
  }

  {
    // Test a sorted pair associative container.
    map<int, string> m;
    m[20] = "twenty";
    m[10] = "ten";
    m[30] = "thirty";
    ostringstream ss;
    ss << m;
    EXPECT_EQ(ss.str(), "(10, ten) (20, twenty) (30, thirty)");
    map<int, string> copied_m(m);
    CHECK_EQ(m, copied_m);  // This must compile.
  }

  {
    // Test a long sequence.
    vector<int> v;
    string expected;
    for (int i = 0; i < 100; i++) {
      v.push_back(i);
      if (i > 0) expected += ' ';
      const size_t buf_size = 256;
      char buf[buf_size];
      std::snprintf(buf, buf_size, "%d", i);
      expected += buf;
    }
    v.push_back(100);
    expected += " ...";
    ostringstream ss;
    ss << v;
    CHECK_EQ(ss.str(), expected.c_str());
  }

  {
    // Test a sorted pair associative container.
    // Use a non-default comparison functor.
    map<int, string, greater<>> m;
    m[20] = "twenty";
    m[10] = "ten";
    m[30] = "thirty";
    ostringstream ss;
    ss << m;
    EXPECT_EQ(ss.str(), "(30, thirty) (20, twenty) (10, ten)");
    map<int, string, greater<>> copied_m(m);
    CHECK_EQ(m, copied_m);  // This must compile.
  }
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
