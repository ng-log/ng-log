// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//
// Unit tests for functions in libbacktrace.cc.

#include "libbacktrace.h"

#include <backtrace.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>

#include "config.h"
#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "ng-log/platform.h"
#include "symbolize.h"

using namespace nglog;
using testing::AllOf;
using testing::HasSubstr;
using testing::Not;

namespace {

constexpr int kLine = 42;
// Fills output buffers before a call so that a test can tell whether the call
// wrote to them.
constexpr char kSentinel = 'X';

}  // namespace

// Tests for FormatLibbacktraceLocation.

TEST(FormatLibbacktraceLocation, ResolvedLocation) {
  constexpr char kExpected[] = "src/libbacktrace.cc:42 ";
  char out[64];
  EXPECT_EQ(std::strlen(kExpected),
            FormatLibbacktraceLocation("src/libbacktrace.cc", kLine, out,
                                       sizeof(out)));
  EXPECT_STREQ(kExpected, out);
}

TEST(FormatLibbacktraceLocation, RejectsNullFilename) {
  char out[64];
  EXPECT_EQ(0U, FormatLibbacktraceLocation(nullptr, kLine, out, sizeof(out)));
}

TEST(FormatLibbacktraceLocation, RejectsEmptyFilename) {
  char out[64];
  EXPECT_EQ(0U, FormatLibbacktraceLocation("", kLine, out, sizeof(out)));
}

TEST(FormatLibbacktraceLocation, RejectsZeroSizedOutputBuffer) {
  char out[] = {kSentinel};
  EXPECT_EQ(0U,
            FormatLibbacktraceLocation("src/libbacktrace.cc", kLine, out, 0));
  EXPECT_EQ(kSentinel, out[0]);
}

TEST(FormatLibbacktraceLocation, SucceedsWhenOutputExactlyFits) {
  constexpr char kExpected[] = "a.cc:1 ";
  char out[sizeof(kExpected)];
  EXPECT_EQ(std::strlen(kExpected),
            FormatLibbacktraceLocation("a.cc", 1, out, sizeof(out)));
  EXPECT_STREQ(kExpected, out);
}

// A result that does not fit is not returned partially written.
TEST(FormatLibbacktraceLocation, RejectsWhenOutputIsOneByteTooSmall) {
  constexpr char kExpected[] = "a.cc:1 ";
  char out[sizeof(kExpected) - 1];
  std::fill(std::begin(out), std::end(out), kSentinel);
  EXPECT_EQ(0U, FormatLibbacktraceLocation("a.cc", 1, out, sizeof(out)));
  EXPECT_EQ('\0', out[0]);
}

namespace {

// The line on which the definition below starts, which the debug info
// reports for the entry address of the function.
constexpr int kSymbolizedFunctionLine = __LINE__ + 2;
NGLOG_ATTRIBUTE_NOINLINE
void FunctionSymbolizedForLibbacktraceTest() {
  volatile int a = 0;
  a = a + 1;
}

void* SymbolizedFunctionAddress() {
  return reinterpret_cast<void*>(&FunctionSymbolizedForLibbacktraceTest);
}

// Defined so that its address can stand in for a program counter without a
// source location.
int g_data_without_location = 0;

// Asks libbacktrace itself, bypassing the code under test, whether the debug
// info of this binary names the source file of |pc|. It does not when the
// binary was built without debug info.
bool HasDebugInfo(void* pc) {
  bool found = false;
  backtrace_pcinfo(
      GetBacktraceState(), reinterpret_cast<std::uintptr_t>(pc),
      [](void* data, std::uintptr_t /*pc*/, const char* filename,
         int /*lineno*/, const char* /*function*/) {
        *static_cast<bool*>(data) = filename != nullptr;
        return 1;
      },
      [](void* /*data*/, const char* /*msg*/, int /*errnum*/) {}, &found);
  return found;
}

}  // namespace

// Exercises LibbacktraceSymbolizeCallback, mostly through Symbolize(). Every
// test that needs the callback installs it itself, so the fixture only
// restores the defaults that main() establishes.
class LibbacktraceSymbolizeCallback : public testing::Test {
 protected:
  static bool InstallCallback(bool line_info) {
    nglog::SetFlag(FLAGS_symbolize_line_info, line_info);
    return InstallLibbacktraceSymbolizeCallback();
  }

  void TearDown() override {
    nglog::SetFlag(FLAGS_symbolize_line_info, true);
    InstallSymbolizeCallback(nullptr);
  }

  static constexpr std::size_t kSymbolBufferSize = 4096;
  char symbol_[kSymbolBufferSize];
};

TEST_F(LibbacktraceSymbolizeCallback, ResolvesFileAndLine) {
  if (!HasDebugInfo(SymbolizedFunctionAddress())) {
    GTEST_SKIP() << "libbacktrace has no debug info for this binary";
  }

  EXPECT_TRUE(InstallCallback(true));
  ASSERT_TRUE(Symbolize(SymbolizedFunctionAddress(), symbol_, sizeof(symbol_)));
  const std::string location =
      "libbacktrace_unittest.cc:" + std::to_string(kSymbolizedFunctionLine) +
      " ";
  EXPECT_THAT(std::string{symbol_},
              AllOf(HasSubstr(location),
                    HasSubstr("FunctionSymbolizedForLibbacktraceTest")));
}

TEST_F(LibbacktraceSymbolizeCallback, DisabledByFlag) {
  EXPECT_TRUE(InstallCallback(false));
  ASSERT_TRUE(Symbolize(SymbolizedFunctionAddress(), symbol_, sizeof(symbol_)));
  EXPECT_THAT(symbol_,
              AllOf(Not(HasSubstr("libbacktrace_unittest.cc:")),
                    HasSubstr("FunctionSymbolizedForLibbacktraceTest")));
}

TEST_F(LibbacktraceSymbolizeCallback, NoDebugInfoForAddress) {
  nglog::SetFlag(FLAGS_symbolize_line_info, true);
  // The address of data rather than code lies within this binary, but no line
  // table of its debug info covers it, so backtrace_pcinfo() reports no
  // location for it. Exercises the "not found" path in
  // LibbacktraceSymbolizeCallback() the way a stack frame in a module without
  // debug info would. An address below every loaded module does not do so
  // reliably: with MinGW, libbacktrace attributes some of them to unrelated
  // source lines.
  void* const address = &g_data_without_location;
  // The callback ignores both of these.
  constexpr int kNoObjectFile = -1;
  constexpr std::uint64_t kNoRelocation = 0;
  std::fill(std::begin(symbol_), std::end(symbol_), kSentinel);
  EXPECT_EQ(
      0, nglog::LibbacktraceSymbolizeCallback(kNoObjectFile, address, symbol_,
                                              sizeof(symbol_), kNoRelocation));
  EXPECT_EQ(kSentinel, symbol_[0]);
}

// Tests for the SymbolizeAndDemangle() branch itself, exercised through
// Symbolize() directly.

TEST(Symbolize, RejectsZeroSizedOutputBuffer) {
  char out[] = {kSentinel};
  EXPECT_FALSE(Symbolize(SymbolizedFunctionAddress(), out, 0));
  EXPECT_EQ(kSentinel, out[0]);
}

TEST_F(LibbacktraceSymbolizeCallback, RejectsTruncatedSymbolName) {
  InstallCallback(false);
  constexpr std::size_t kOutputSize = 8;
  char out[kOutputSize];
  EXPECT_FALSE(Symbolize(SymbolizedFunctionAddress(), out, sizeof(out),
                         SymbolizeOptions::kNoLineNumbers));
}

int main(int argc, char** argv) {
  nglog::SetFlag(FLAGS_logtostderr, true);
  InitializeLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);
  // Independent from whatever InitializeLogging() may have installed via
  // FLAGS_symbolize_line_info, so tests don't depend on flag defaults.
  InstallSymbolizeCallback(nullptr);
  return RUN_ALL_TESTS();
}
