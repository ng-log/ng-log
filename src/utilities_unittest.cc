// Copyright (c) 2008, Google Inc.
// Copyright (c) 2026, The ng-log contributors
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Author: Shinichiro Hamaji
#include "utilities.h"

#include <gtest/gtest.h>

#include "ng-log/logging.h"
#include "testing_utilities.h"

#ifdef NGLOG_USE_GFLAGS
#  include <gflags/gflags.h>
using namespace GFLAGS_NAMESPACE;
#endif

using namespace nglog;

TEST(utilities, InitializeLoggingDeathTest) {
  ASSERT_DEATH(InitializeLogging("foobar"), "");
}

TEST(utilities, CollapseRepeatedCharacters) {
  constexpr char delimiters[] = {'/', '\\'};

  EXPECT_EQ(nglog::CollapseRepeatedCharacters("one///two\\\\three", delimiters),
            "one/two\\three");
  EXPECT_EQ(nglog::CollapseRepeatedCharacters("one///two", delimiters,
                                              sizeof(delimiters)),
            "one/two");
  EXPECT_EQ(nglog::CollapseRepeatedCharacters("aabbbaac", "ab"), "ac");
  EXPECT_EQ(nglog::CollapseRepeatedCharacters("unchanged", delimiters),
            "unchanged");
}

TEST(utilities, IsFilenameExtensionAfterBaseFilename) {
  EXPECT_TRUE(nglog::IsFilenameExtensionAfterBaseFilename(
      "/tmp/app.custom.20260817-123456.42", "/tmp/app", ".custom"));
  EXPECT_FALSE(nglog::IsFilenameExtensionAfterBaseFilename(
      "/tmp/app.20260817-123456.42", "/tmp/app", ".custom"));
}

TEST(utilities, IsFilenameExtensionAtEnd) {
  EXPECT_TRUE(nglog::IsFilenameExtensionAtEnd(
      "/tmp/app.20260817-123456.42.custom", ".custom"));
  EXPECT_FALSE(nglog::IsFilenameExtensionAtEnd(
      "/tmp/app.20260817-123456.42.other", ".custom"));
  EXPECT_FALSE(nglog::IsFilenameExtensionAtEnd(".custom", ".custom"));
}

TEST(utilities, TrimTrailingCRLFRemovesTrailingNewlines) {
  EXPECT_EQ(nglog::TrimTrailingCRLF("message\r\n"), "message");
  EXPECT_EQ(nglog::TrimTrailingCRLF("message\n"), "message");
  EXPECT_EQ(nglog::TrimTrailingCRLF("message"), "message");
  EXPECT_EQ(nglog::TrimTrailingCRLF("\r\n"), "");
}

TEST(utilities, TrimTrailingCharacters) {
  constexpr char delimiters[] = {' ', '\t'};

  EXPECT_EQ(nglog::TrimTrailingCharacters("message \t", delimiters), "message");
  EXPECT_EQ(nglog::TrimTrailingCharacters("message \t", delimiters,
                                          sizeof(delimiters)),
            "message");
  EXPECT_EQ(nglog::TrimTrailingCharacters("message", " \t"), "message");
  EXPECT_EQ(nglog::TrimTrailingCharacters(" \t", delimiters), "");
}

int main(int argc, char** argv) {
  InitializeLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);

  CHECK_EQ(RUN_ALL_TESTS(), 0);
}
