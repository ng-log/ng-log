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

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "internal/utf8.h"
#include "ng-log/logging.h"
#include "testing_utilities.h"

#ifdef NGLOG_OS_WINDOWS
#  include <fcntl.h>
#  include <io.h>
#  include <windows.h>
#endif

#ifdef NGLOG_USE_GFLAGS
#  include <gflags/gflags.h>
using namespace GFLAGS_NAMESPACE;
#endif

using namespace nglog;

TEST(utilities, InitializeLoggingDeathTest) {
  ASSERT_DEATH(InitializeLogging("foobar"), "");
}

TEST(Utf8Output, PreservesRedirectedBytes) {
  const std::string message = "caf\xC3\xA9 \xE2\x98\x83";
  std::FILE* file = std::tmpfile();
  ASSERT_NE(file, nullptr);

  EXPECT_TRUE(nglog::internal::WriteUtf8(file, message.data(), message.size()));
  EXPECT_EQ(std::ftell(file), static_cast<long>(message.size()));
  ASSERT_EQ(std::fflush(file), 0);
  ASSERT_EQ(std::fseek(file, 0, SEEK_SET), 0);

  std::string output(message.size(), '\0');
  ASSERT_EQ(std::fread(output.data(), 1, output.size(), file), output.size());
  EXPECT_EQ(output, message);
  std::fclose(file);
}

TEST(Utf8Output, PreservesLengthDelimitedBytes) {
  const char message[] = {'a', '\0', '\xFF', 'b'};
  std::FILE* file = std::tmpfile();
  ASSERT_NE(file, nullptr);

  EXPECT_TRUE(nglog::internal::WriteUtf8(file, message, sizeof(message)));
  ASSERT_EQ(std::fflush(file), 0);
  ASSERT_EQ(std::fseek(file, 0, SEEK_SET), 0);

  char output[sizeof(message)] = {};
  ASSERT_EQ(std::fread(output, 1, sizeof(output), file), sizeof(output));
  EXPECT_EQ(std::memcmp(output, message, sizeof(message)), 0);
  std::fclose(file);
}

#ifdef NGLOG_OS_WINDOWS
TEST(Utf8Output, WritesUnicodeToConsole) {
  HANDLE console = CreateConsoleScreenBuffer(
      GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
      CONSOLE_TEXTMODE_BUFFER, nullptr);
  if (console == INVALID_HANDLE_VALUE) {
    GTEST_SKIP() << "Windows console is unavailable";
  }

  const int file_descriptor = _open_osfhandle(
      reinterpret_cast<intptr_t>(console), _O_WRONLY | _O_BINARY);
  ASSERT_NE(file_descriptor, -1);

  const std::string message = "caf\xC3\xA9 \xE2\x98\x83";
  ASSERT_TRUE(nglog::internal::WriteUtf8ToFileDescriptor(
      file_descriptor, message.data(), message.size()));

  wchar_t output[7] = {};
  COORD position = {0, 0};
  DWORD characters_read = 0;
  ASSERT_TRUE(ReadConsoleOutputCharacterW(console, output, 6, position,
                                          &characters_read));
  ASSERT_EQ(characters_read, 6U);
  EXPECT_EQ(std::wstring(L"caf\u00e9 \u2603"),
            std::wstring(output, characters_read));
  _close(file_descriptor);
}
#endif

int main(int argc, char** argv) {
  InitializeLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);

  CHECK_EQ(RUN_ALL_TESTS(), 0);
}
