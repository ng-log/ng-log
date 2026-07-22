// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//
// Unit tests for functions in libbacktrace.cc.

#include "libbacktrace.h"

#include <gtest/gtest.h>

#include <cstring>
#include <iostream>

#include "config.h"
#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "symbolize.h"
#include "utilities.h"

#ifdef NGLOG_USE_GFLAGS
#  include <gflags/gflags.h>
using namespace GFLAGS_NAMESPACE;
#endif  // NGLOG_USE_GFLAGS

using namespace nglog;

// Tests for FormatLibbacktraceLocation.

TEST(FormatLibbacktraceLocation, ResolvedLocation) {
  char out[64];
  const std::size_t written =
      FormatLibbacktraceLocation("src/libbacktrace.cc", 42, out, sizeof(out));
  EXPECT_EQ(std::strlen("src/libbacktrace.cc:42 "), written);
  EXPECT_STREQ("src/libbacktrace.cc:42 ", out);
}

TEST(FormatLibbacktraceLocation, RejectsNullFilename) {
  char out[64];
  EXPECT_EQ(0U, FormatLibbacktraceLocation(nullptr, 42, out, sizeof(out)));
}

TEST(FormatLibbacktraceLocation, RejectsEmptyFilename) {
  char out[64];
  EXPECT_EQ(0U, FormatLibbacktraceLocation("", 42, out, sizeof(out)));
}

TEST(FormatLibbacktraceLocation, RejectsZeroSizedOutputBuffer) {
  char out[64];
  EXPECT_EQ(0U, FormatLibbacktraceLocation("src/libbacktrace.cc", 42, out, 0));
}

TEST(FormatLibbacktraceLocation, RejectsWhenOutputDoesNotFit) {
  constexpr char kFilename[] = "src/libbacktrace.cc";
  char out[8];  // Too small for "src/libbacktrace.cc:42 " + '\0'.
  EXPECT_EQ(0U, FormatLibbacktraceLocation(kFilename, 42, out, sizeof(out)));
}

TEST(FormatLibbacktraceLocation, SucceedsWhenOutputExactlyFits) {
  constexpr char kFilename[] = "a.cc";
  constexpr char kExpected[] = "a.cc:1 ";
  char out[sizeof(kExpected)];
  const std::size_t written =
      FormatLibbacktraceLocation(kFilename, 1, out, sizeof(out));
  EXPECT_EQ(std::strlen(kExpected), written);
  EXPECT_STREQ(kExpected, out);
}

TEST(FormatLibbacktraceLocation, RejectsWhenOutputIsOneByteTooSmall) {
  constexpr char kFilename[] = "a.cc";
  constexpr char kExpected[] = "a.cc:1 ";
  char out[sizeof(kExpected) - 1];
  EXPECT_EQ(0U, FormatLibbacktraceLocation(kFilename, 1, out, sizeof(out)));
}

// Tests for LibbacktraceSymbolizeCallback, exercised through Symbolize().

namespace {

void ATTRIBUTE_NOINLINE FunctionSymbolizedForLibbacktraceTest() {
  volatile int a = 0;
  a = a + 1;
}

const char* TrySymbolize(void* pc, char* out, std::size_t out_size) {
  if (Symbolize(pc, out, out_size)) {
    return out;
  }
  return nullptr;
}

}  // namespace

TEST(LibbacktraceSymbolizeCallback, ResolvesFileAndLine) {
  FLAGS_symbolize_line_info = true;
  EXPECT_TRUE(InstallLibbacktraceSymbolizeCallback());

  char symbol[4096];
  const char* result = TrySymbolize(
      reinterpret_cast<void*>(&FunctionSymbolizedForLibbacktraceTest), symbol,
      sizeof(symbol));
  CHECK(result != nullptr);
  InstallSymbolizeCallback(nullptr);

  if (std::strstr(result, "libbacktrace_unittest.cc:") == nullptr) {
    std::cerr << "libbacktrace has no debug info for this binary: skipping"
              << std::endl;
    return;
  }
  EXPECT_NE(nullptr,
            std::strstr(result, "FunctionSymbolizedForLibbacktraceTest"));
}

TEST(LibbacktraceSymbolizeCallback, DisabledByFlag) {
  FLAGS_symbolize_line_info = false;
  EXPECT_TRUE(InstallLibbacktraceSymbolizeCallback());

  char symbol[4096];
  const char* result = TrySymbolize(
      reinterpret_cast<void*>(&FunctionSymbolizedForLibbacktraceTest), symbol,
      sizeof(symbol));
  CHECK(result != nullptr);
  EXPECT_EQ(nullptr, std::strstr(result, "libbacktrace_unittest.cc:"));
  EXPECT_NE(nullptr,
            std::strstr(result, "FunctionSymbolizedForLibbacktraceTest"));

  FLAGS_symbolize_line_info = true;
  InstallSymbolizeCallback(nullptr);
}

TEST(LibbacktraceSymbolizeCallback, NoDebugInfoForAddress) {
  FLAGS_symbolize_line_info = true;
  EXPECT_TRUE(InstallLibbacktraceSymbolizeCallback());

  char symbol[4096];
  // An address this low can never fall inside a loaded module, so
  // backtrace_pcinfo() reports no location for it. Exercises the "not
  // found" path in LibbacktraceSymbolizeCallback() the way a stack frame
  // in a module without debug info would.
  const char* result =
      TrySymbolize(reinterpret_cast<void*>(0x1), symbol, sizeof(symbol));
  EXPECT_EQ(nullptr, result);

  InstallSymbolizeCallback(nullptr);
}

// Tests for the SymbolizeAndDemangle() branch itself, exercised through
// Symbolize() directly.

TEST(Symbolize, RejectsZeroSizedOutputBuffer) {
  char out[1];
  EXPECT_FALSE(Symbolize(
      reinterpret_cast<void*>(&FunctionSymbolizedForLibbacktraceTest), out, 0));
}

TEST(Symbolize, RejectsTruncatedSymbolName) {
  constexpr std::size_t kOutputSize = 8;
  char out[kOutputSize];
  FLAGS_symbolize_line_info = false;
  InstallLibbacktraceSymbolizeCallback();

  EXPECT_FALSE(
      Symbolize(reinterpret_cast<void*>(&FunctionSymbolizedForLibbacktraceTest),
                out, sizeof(out), SymbolizeOptions::kNoLineNumbers));

  FLAGS_symbolize_line_info = true;
  InstallSymbolizeCallback(nullptr);
}

int main(int argc, char** argv) {
  FLAGS_logtostderr = true;
  InitializeLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);
  // Independent from whatever InitializeLogging() may have installed via
  // FLAGS_symbolize_line_info, so tests don't depend on flag defaults.
  InstallSymbolizeCallback(nullptr);
  return RUN_ALL_TESTS();
}
