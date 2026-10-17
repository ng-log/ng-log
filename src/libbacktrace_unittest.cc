// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//
// Unit tests for functions in libbacktrace.cc.

#include "libbacktrace.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

#include "config.h"
#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "symbolize.h"
#include "utilities.h"

using namespace nglog;
using testing::AllOf;
using testing::HasSubstr;
using testing::Not;

namespace {

constexpr int kLine = 42;

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
  char out[64];
  EXPECT_EQ(0U,
            FormatLibbacktraceLocation("src/libbacktrace.cc", kLine, out, 0));
}

TEST(FormatLibbacktraceLocation, RejectsWhenOutputDoesNotFit) {
  char out[8];  // Too small for "src/libbacktrace.cc:42 " + '\0'.
  EXPECT_EQ(0U, FormatLibbacktraceLocation("src/libbacktrace.cc", kLine, out,
                                           sizeof(out)));
}

TEST(FormatLibbacktraceLocation, SucceedsWhenOutputExactlyFits) {
  constexpr char kExpected[] = "a.cc:1 ";
  char out[sizeof(kExpected)];
  EXPECT_EQ(std::strlen(kExpected),
            FormatLibbacktraceLocation("a.cc", 1, out, sizeof(out)));
  EXPECT_STREQ(kExpected, out);
}

TEST(FormatLibbacktraceLocation, RejectsWhenOutputIsOneByteTooSmall) {
  constexpr char kExpected[] = "a.cc:1 ";
  char out[sizeof(kExpected) - 1];
  EXPECT_EQ(0U, FormatLibbacktraceLocation("a.cc", 1, out, sizeof(out)));
}

namespace {

NGLOG_ATTRIBUTE_NOINLINE
void FunctionSymbolizedForLibbacktraceTest() {
  volatile int a = 0;
  a = a + 1;
}

void* SymbolizedFunctionAddress() {
  return reinterpret_cast<void*>(&FunctionSymbolizedForLibbacktraceTest);
}

}  // namespace

// Exercises LibbacktraceSymbolizeCallback through Symbolize(). Every test
// installs the callback itself, so the fixture only restores the defaults
// that main() establishes.
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
  EXPECT_TRUE(InstallCallback(true));
  ASSERT_TRUE(Symbolize(SymbolizedFunctionAddress(), symbol_, sizeof(symbol_)));

  if (!testing::Value(symbol_, HasSubstr("libbacktrace_unittest.cc:"))) {
    GTEST_SKIP() << "libbacktrace has no debug info for this binary";
  }
  EXPECT_THAT(symbol_, HasSubstr("FunctionSymbolizedForLibbacktraceTest"));
}

TEST_F(LibbacktraceSymbolizeCallback, DisabledByFlag) {
  EXPECT_TRUE(InstallCallback(false));
  ASSERT_TRUE(Symbolize(SymbolizedFunctionAddress(), symbol_, sizeof(symbol_)));
  EXPECT_THAT(symbol_,
              AllOf(Not(HasSubstr("libbacktrace_unittest.cc:")),
                    HasSubstr("FunctionSymbolizedForLibbacktraceTest")));
}

TEST_F(LibbacktraceSymbolizeCallback, NoDebugInfoForAddress) {
  EXPECT_TRUE(InstallCallback(true));
  // An address this low can never fall inside a loaded module, so
  // backtrace_pcinfo() reports no location for it. Exercises the "not
  // found" path in LibbacktraceSymbolizeCallback() the way a stack frame
  // in a module without debug info would.
  constexpr std::uintptr_t kUnmappedAddress = 0x1;
  EXPECT_FALSE(Symbolize(reinterpret_cast<void*>(kUnmappedAddress), symbol_,
                         sizeof(symbol_)));
}

// Tests for the SymbolizeAndDemangle() branch itself, exercised through
// Symbolize() directly.

TEST(Symbolize, RejectsZeroSizedOutputBuffer) {
  char out[1];
  EXPECT_FALSE(Symbolize(SymbolizedFunctionAddress(), out, 0));
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
