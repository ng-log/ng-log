// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//
// Unit tests for functions in addr2line.cc.

#include "addr2line.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ng-log/platform.h"

#ifdef NGLOG_OS_WINDOWS
#  include <process.h>
#  include <windows.h>
#else
#  include <sys/stat.h>
#  include <unistd.h>
#endif  // NGLOG_OS_WINDOWS

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "config.h"
#include "internal/utf8.h"
#ifdef NGLOG_OS_WINDOWS
#  include "long_path_test.h"
#endif
#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "symbolize.h"
#include "testing_utilities.h"
#include "utilities.h"

using namespace nglog;

namespace {

#ifdef NGLOG_OS_WINDOWS
bool g_long_path_child = false;
#endif

using UniqueFile = std::unique_ptr<std::FILE, int (*)(std::FILE*)>;

constexpr std::size_t kCopyBufferSize = 4096;

// Creates a fresh, empty scratch directory in the private temporary
// directory of the test process and returns its path. CHECK-fails on error.
std::string MakeScratchDirectory() {
  static unsigned int counter = 0;
  const std::string directory =
      TestTmpDir() + "/addr2line_" + std::to_string(++counter);
#ifdef NGLOG_OS_WINDOWS
  std::wstring wide_directory;
  CHECK(internal::Utf8ToWide(directory.data(), directory.size(),
                             &wide_directory));
  CHECK(CreateDirectoryW(wide_directory.c_str(), nullptr));
#else
  constexpr mode_t kDirectoryMode = 0700;
  CHECK_EQ(0, mkdir(directory.c_str(), kDirectoryMode));
#endif  // NGLOG_OS_WINDOWS
  return directory;
}

// Materializes a fake "addr2line" executable in |dir| and returns its path.
// |helper_path| is a companion program built alongside this test that already
// exhibits the needed behavior.
std::string MakeFakeAddr2Line(const std::string& dir, const char* helper_path) {
#ifdef NGLOG_OS_WINDOWS
  // CreateProcess() appends ".exe" automatically to an extension-less
  // name, matching how the production code spawns "addr2line".
  const std::string path = dir + "\\addr2line.exe";
  std::wstring wide_helper_path;
  std::wstring wide_path;
  CHECK(internal::Utf8ToWide(helper_path, std::strlen(helper_path),
                             &wide_helper_path));
  CHECK(internal::Utf8ToWide(path.data(), path.size(), &wide_path));
  CHECK(CopyFileW(wide_helper_path.c_str(), wide_path.c_str(), FALSE));
  return path;
#else
  const std::string path = dir + "/addr2line";
  UniqueFile source(std::fopen(helper_path, "rb"), &std::fclose);
  CHECK(source != nullptr);
  UniqueFile destination(std::fopen(path.c_str(), "wb"), &std::fclose);
  CHECK(destination != nullptr);

  char buffer[kCopyBufferSize];
  std::size_t bytes_read;
  while ((bytes_read = std::fread(buffer, 1, sizeof(buffer), source.get())) >
         0) {
    CHECK_EQ(bytes_read, std::fwrite(buffer, 1, bytes_read, destination.get()));
  }
  CHECK_EQ(0, std::ferror(source.get()));
  destination.reset();
  constexpr mode_t kExecutableMode = 0755;
  CHECK_EQ(0, chmod(path.c_str(), kExecutableMode));
  return path;
#endif  // NGLOG_OS_WINDOWS
}

// Places a copy of |helper_path| named addr2line into a fresh scratch
// directory and points PATH at that directory while in scope. The private
// temporary directory of the test process removes the copy on exit.
class ScopedFakeAddr2Line {
 public:
  explicit ScopedFakeAddr2Line(const char* helper_path)
      : directory_(MakeScratchDirectory()),
        path_override_("PATH", directory_.c_str()) {
    MakeFakeAddr2Line(directory_, helper_path);
  }

 private:
  std::string directory_;
  ScopedEnvironmentVariable path_override_;
};

}  // namespace

// Tests for FormatAddr2LineOutput.

namespace {

constexpr std::size_t kFormatBufferSize = 64;
constexpr char kLocationInput[] = "src/addr2line.cc:42\n";
constexpr char kFormattedLocation[] = "src/addr2line.cc:42 ";

// addr2line output that carries a usable location and its expected
// formatting.
struct FormatCase {
  const char* name;
  const char* input;
  const char* expected;
};

// addr2line output that a parser rejects.
struct RejectedInput {
  const char* name;
  const char* input;
};

class FormatAddr2LineOutputAccepts
    : public ::testing::TestWithParam<FormatCase> {};

TEST_P(FormatAddr2LineOutputAccepts, WritesFileAndLine) {
  const FormatCase& param = GetParam();
  char out[kFormatBufferSize];
  const std::size_t written = FormatAddr2LineOutput(
      param.input, std::strlen(param.input), out, sizeof(out));
  EXPECT_EQ(std::strlen(param.expected), written);
  EXPECT_STREQ(param.expected, out);
}

INSTANTIATE_TEST_SUITE_P(
    FormatAddr2LineOutput, FormatAddr2LineOutputAccepts,
    ::testing::Values(
        FormatCase{"ResolvedLocation", kLocationInput, kFormattedLocation},
        // GNU addr2line appends " (discriminator N)" when an address maps
        // to one of several DWARF line-table discriminators for the same
        // source line, as commonly happens right at a CHECK/LOG(FATAL) call
        // site.
        FormatCase{"IgnoresTrailingDiscriminatorAnnotation",
                   "src/addr2line.cc:42 (discriminator 3)\n",
                   kFormattedLocation},
        FormatCase{"IgnoresLinesAfterTheFirst",
                   "src/addr2line.cc:42\nsrc/other.cc:1\n", kFormattedLocation},
        FormatCase{"HandlesInputWithoutTrailingNewline", "src/addr2line.cc:42",
                   kFormattedLocation},
        FormatCase{"HandlesColonWithinTheFilePath", "weird:name.cc:42\n",
                   "weird:name.cc:42 "}),
    CaseName{});

class FormatAddr2LineOutputRejects
    : public ::testing::TestWithParam<RejectedInput> {};

TEST_P(FormatAddr2LineOutputRejects, ReturnsZero) {
  const RejectedInput& param = GetParam();
  char out[kFormatBufferSize];
  EXPECT_EQ(0U, FormatAddr2LineOutput(param.input, std::strlen(param.input),
                                      out, sizeof(out)));
}

INSTANTIATE_TEST_SUITE_P(
    FormatAddr2LineOutput, FormatAddr2LineOutputRejects,
    ::testing::Values(RejectedInput{"UnresolvedLocation", "??:0\n"},
                      RejectedInput{"MissingColon", "not-a-location\n"},
                      RejectedInput{"NonNumericLine", "src/addr2line.cc:abc\n"},
                      RejectedInput{"EmptyInput", ""},
                      RejectedInput{"EmptyFileField", ":42\n"},
                      RejectedInput{"EmptyLineField", "src/addr2line.cc:\n"},
                      // A byte with the high bit set is never a valid
                      // line-number digit. This also exercises that classifying
                      // it does not read out of bounds.
                      RejectedInput{"HighBitSetByteInLineField",
                                    "src/addr2line.cc:\xff\n"}),
    CaseName{});

// An output buffer size too small for the formatted location and its NUL.
struct ShortBufferCase {
  const char* name;
  std::size_t size;
};

class FormatAddr2LineOutputRejectsShortBuffer
    : public ::testing::TestWithParam<ShortBufferCase> {};

TEST_P(FormatAddr2LineOutputRejectsShortBuffer, ReturnsZero) {
  const ShortBufferCase& param = GetParam();
  // Sized exactly so that sanitizers flag any write past the given size.
  const std::unique_ptr<char[]> out{new char[param.size]};
  EXPECT_EQ(0U,
            FormatAddr2LineOutput(kLocationInput, std::strlen(kLocationInput),
                                  out.get(), param.size));
}

INSTANTIATE_TEST_SUITE_P(
    FormatAddr2LineOutput, FormatAddr2LineOutputRejectsShortBuffer,
    ::testing::Values(
        ShortBufferCase{"ZeroSized", 0},
        // Room for a NUL and a byte of content, but not even the fixed ':'
        // and ' ' that always separate the file and line fields.
        ShortBufferCase{"TooSmallForTheLiteralBytes", 2},
        ShortBufferCase{"FarTooSmall", 8},
        ShortBufferCase{"OneByteTooSmall", sizeof(kFormattedLocation) - 1}),
    CaseName{});

}  // namespace

TEST(FormatAddr2LineOutput, SucceedsWhenOutputExactlyFits) {
  const std::unique_ptr<char[]> out{new char[sizeof(kFormattedLocation)]};
  const std::size_t written =
      FormatAddr2LineOutput(kLocationInput, std::strlen(kLocationInput),
                            out.get(), sizeof(kFormattedLocation));
  EXPECT_EQ(std::strlen(kFormattedLocation), written);
  EXPECT_STREQ(kFormattedLocation, out.get());
}

// Tests for SplitAddr2LineFunctionsOutput.

TEST(SplitAddr2LineFunctionsOutput, ResolvesNameAndLocation) {
  constexpr char kInput[] = "main\nsrc/addr2line.cc:42\n";
  Addr2LineFunctionsResult result;
  ASSERT_TRUE(
      SplitAddr2LineFunctionsOutput(kInput, std::strlen(kInput), &result));
  EXPECT_EQ(std::string(result.name, result.name_len), "main");
  EXPECT_EQ(std::string(result.file_line, result.file_line_len),
            "src/addr2line.cc:42");
}

TEST(SplitAddr2LineFunctionsOutput, ResolvesNameWithCrlfLineEndings) {
  constexpr char kInput[] = "main\r\nsrc/addr2line.cc:42\r\n";
  Addr2LineFunctionsResult result;
  ASSERT_TRUE(
      SplitAddr2LineFunctionsOutput(kInput, std::strlen(kInput), &result));
  EXPECT_EQ(std::string(result.name, result.name_len), "main");
}

namespace {

class SplitAddr2LineFunctionsOutputRejects
    : public ::testing::TestWithParam<RejectedInput> {};

TEST_P(SplitAddr2LineFunctionsOutputRejects, ReturnsFalse) {
  const RejectedInput& param = GetParam();
  Addr2LineFunctionsResult result;
  EXPECT_FALSE(SplitAddr2LineFunctionsOutput(
      param.input, std::strlen(param.input), &result));
}

INSTANTIATE_TEST_SUITE_P(
    SplitAddr2LineFunctionsOutput, SplitAddr2LineFunctionsOutputRejects,
    ::testing::Values(
        RejectedInput{"UnresolvedName", "??\n??:0\n"},
        // addr2line's stdout is opened in text mode on Windows, so its CRT
        // writes CRLF line endings. The "??" placeholder must still be
        // recognized with the trailing '\r' included in the line.
        RejectedInput{"UnresolvedNameWithCrlfLineEndings", "??\r\n??:0\r\n"},
        RejectedInput{"MissingSecondLine", "main"},
        RejectedInput{"EmptyNameField", "\nsrc/addr2line.cc:42\n"}),
    CaseName{});

}  // namespace

// Tests for Addr2LineSymbolizeCallback, exercised through Symbolize().
// This whole file only builds when HAVE_ADDR2LINE is defined, which
// always implies HAVE_SYMBOLIZE on every platform this backend
// supports, so Symbolize() below is always the real implementation,
// never a stub.

namespace {

constexpr std::size_t kSymbolBufferSize = 4096;
constexpr char kSourceLocation[] = "addr2line_unittest.cc:";
constexpr char kFunctionName[] = "FunctionSymbolizedForAddr2LineTest";
constexpr char kResolverUnavailable[] =
    "no name resolver available without addr2line";

NGLOG_ATTRIBUTE_NOINLINE
void FunctionSymbolizedForAddr2LineTest() {
  volatile int a = 0;
  a = a + 1;
}

const char* TrySymbolize(void* pc) {
  static char symbol[kSymbolBufferSize];
  if (Symbolize(pc, symbol, sizeof(symbol))) {
    return symbol;
  }
  return nullptr;
}

const char* TrySymbolizeTestFunction() {
  return TrySymbolize(
      reinterpret_cast<void*>(&FunctionSymbolizedForAddr2LineTest));
}

// Resolves an address within a module that does not exist, so that only the
// addr2line executable found on PATH determines the outcome.
bool ResolveInMissingModule(char (&symbol)[kSymbolBufferSize]) {
  constexpr std::uintptr_t kAddress = 1;
  constexpr std::uint64_t kRelocation = 0;
  return ResolveFunctionAndLine(
      "missing.exe", reinterpret_cast<void*>(kAddress), kRelocation, symbol,
      sizeof(symbol), SymbolizeOptions::kNone, nullptr);
}

// Matches a symbol that names the test function without a source location.
::testing::Matcher<const char*> NamesFunctionWithoutLocation() {
  return ::testing::AllOf(
      ::testing::HasSubstr(kFunctionName),
      ::testing::Not(::testing::HasSubstr(kSourceLocation)));
}

#ifdef NGLOG_OS_WINDOWS
bool RunLongPathSymbolizeChild(DWORD* create_error) {
  *create_error = ERROR_SUCCESS;
  nglog::testing_internal::LongPathTestDirectory directory;
  const std::string directory_name =
      nglog::testing_internal::UniqueTemporaryName(
          "nglog_symbolize_long_path_");
  if (!directory.CreateInTemporaryDirectory(directory_name)) {
    return false;
  }

  std::wstring executable_path;
  if (!nglog::testing_internal::GetExecutablePath(&executable_path)) {
    return false;
  }

  constexpr wchar_t kChildName[] = L"long_path_symbolize.exe";
  const std::wstring child_path = directory.ExtendedChildPath(kChildName);
  if (!CopyFileW(executable_path.c_str(), child_path.c_str(), FALSE)) {
    return false;
  }

  std::wstring command_line = L'"' + child_path +
                              L"\" --gtest_filter="
                              L"Addr2LineSymbolizeCallback."
                              L"ResolvesFromLongModulePath "
                              L"--long-path-child";
  STARTUPINFOW startup_info = {};
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process_info = {};
  const BOOL created =
      CreateProcessW(child_path.c_str(), &command_line[0], nullptr, nullptr,
                     FALSE, 0, nullptr, nullptr, &startup_info, &process_info);
  if (!created) {
    *create_error = GetLastError();
    DeleteFileW(child_path.c_str());
    return false;
  }

  WaitForSingleObject(process_info.hProcess, INFINITE);
  DWORD exit_code = 0;
  const BOOL read_exit_code =
      GetExitCodeProcess(process_info.hProcess, &exit_code);
  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);
  DeleteFileW(child_path.c_str());
  return read_exit_code && exit_code == 0;
}
#endif

// Restores the symbolization callback and the flags that tests modify.
class Addr2LineSymbolizeCallback : public ::testing::Test {
 protected:
  // Lets addr2line finish however long a loaded machine stalls it, in tests
  // where a timeout would take a different path than the one under test.
  static void DisableAddr2LineTimeout() {
    nglog::SetFlag(FLAGS_addr2line_timeout_ms,
                   std::numeric_limits<nglog::int32>::max());
  }

  void TearDown() override {
    InstallSymbolizeCallback(nullptr);
    nglog::SetFlag(FLAGS_symbolize_line_info, symbolize_line_info_);
    nglog::SetFlag(FLAGS_addr2line_timeout_ms, addr2line_timeout_ms_);
  }

 private:
  const bool symbolize_line_info_ = nglog::GetFlag(FLAGS_symbolize_line_info);
  const nglog::int32 addr2line_timeout_ms_ =
      nglog::GetFlag(FLAGS_addr2line_timeout_ms);
};

}  // namespace

// On platforms where addr2line supplements a name resolver that always
// succeeds on its own (the ELF reader on Linux), symbol is never null.
// On the Windows backend, addr2line resolves the name itself, so when it
// cannot (unavailable, or the known same-translation-unit-as-main()
// linker quirk noted in symbolize.cc), Symbolize() legitimately fails
// outright. The tests below treat a null symbol as a reason to skip
// rather than a hard failure.

TEST_F(Addr2LineSymbolizeCallback, ResolvesFileAndLine) {
  nglog::SetFlag(FLAGS_symbolize_line_info, true);
  EXPECT_TRUE(InstallAddr2LineSymbolizeCallback());

  const char* symbol = TrySymbolizeTestFunction();

  if (symbol == nullptr || std::strstr(symbol, kSourceLocation) == nullptr) {
    GTEST_SKIP() << "addr2line not available or not resolving";
  }
  EXPECT_THAT(symbol, ::testing::HasSubstr(kFunctionName));
}

#ifdef NGLOG_OS_WINDOWS
TEST_F(Addr2LineSymbolizeCallback, ResolvesFromLongModulePath) {
  nglog::SetFlag(FLAGS_symbolize_line_info, true);
  ASSERT_TRUE(InstallAddr2LineSymbolizeCallback());
  const char* const symbol =
      TrySymbolize(reinterpret_cast<void*>(&InstallAddr2LineSymbolizeCallback));
  InstallSymbolizeCallback(nullptr);

  if (g_long_path_child) {
    ASSERT_NE(symbol, nullptr);
    EXPECT_THAT(symbol,
                ::testing::HasSubstr("InstallAddr2LineSymbolizeCallback"));
    return;
  }

  if (symbol == nullptr) {
    GTEST_SKIP() << "addr2line not available or not resolving";
  }

  DWORD create_error = ERROR_SUCCESS;
  const bool resolved = RunLongPathSymbolizeChild(&create_error);
  if (!resolved && create_error == ERROR_INVALID_NAME &&
      nglog::testing_internal::IsWine()) {
    GTEST_SKIP() << "Wine does not launch executables through extended paths";
  }
  EXPECT_TRUE(resolved);
}
#endif

TEST_F(Addr2LineSymbolizeCallback, DisabledByFlag) {
  nglog::SetFlag(FLAGS_symbolize_line_info, false);
  EXPECT_TRUE(InstallAddr2LineSymbolizeCallback());

  const char* symbol = TrySymbolizeTestFunction();

  if (symbol == nullptr || std::strstr(symbol, kFunctionName) == nullptr) {
    GTEST_SKIP() << "addr2line not available or not resolving";
  }
  EXPECT_THAT(symbol, ::testing::Not(::testing::HasSubstr(kSourceLocation)));
}

TEST_F(Addr2LineSymbolizeCallback, SkipsGracefullyWhenUnavailable) {
  const ScopedEnvironmentVariable scoped_path{
      "PATH", "/nonexistent/directory/for/testing"};

  nglog::SetFlag(FLAGS_symbolize_line_info, true);
  EXPECT_TRUE(InstallAddr2LineSymbolizeCallback());

  const char* symbol = TrySymbolizeTestFunction();

  if (symbol == nullptr) {
    GTEST_SKIP() << kResolverUnavailable;
  }
  EXPECT_THAT(symbol, NamesFunctionWithoutLocation());
}

TEST_F(Addr2LineSymbolizeCallback, DoesNotHangOnAnUnresponsiveAddr2Line) {
  // Short enough to keep this test fast, while still giving the helper
  // plenty of time to be seen as unresponsive.
  constexpr nglog::int32 kTimeoutMs = 50;

  const char* symbol = nullptr;
  {
    // This helper never writes anything, exercising the timeout /
    // forced-termination path in RunAddr2Line().
    const ScopedFakeAddr2Line fake{ADDR2LINE_FAKE_HANG_PATH};
    nglog::SetFlag(FLAGS_addr2line_timeout_ms, kTimeoutMs);
    nglog::SetFlag(FLAGS_symbolize_line_info, true);
    EXPECT_TRUE(InstallAddr2LineSymbolizeCallback());

    symbol = TrySymbolizeTestFunction();
  }

  if (symbol == nullptr) {
    GTEST_SKIP() << kResolverUnavailable;
  }
  EXPECT_THAT(symbol, NamesFunctionWithoutLocation());
}

TEST_F(Addr2LineSymbolizeCallback, UsesOneTimeoutForSlowOutput) {
  constexpr std::chrono::milliseconds kTimeout{40};
  // The helper never exits and emits a chunk well within kTimeout. A timeout
  // restarted by every read would therefore keep reading until the output
  // buffer fills, which takes over a minute. The bound stays far below that
  // while tolerating long scheduling stalls of a loaded machine.
  constexpr std::chrono::seconds kMaxElapsed{10};

  const ScopedFakeAddr2Line fake{ADDR2LINE_FAKE_STREAM_PATH};
  nglog::SetFlag(FLAGS_addr2line_timeout_ms,
                 static_cast<nglog::int32>(kTimeout.count()));
  EXPECT_TRUE(InstallAddr2LineSymbolizeCallback());

  char symbol[kSymbolBufferSize];
  const auto start = std::chrono::steady_clock::now();
  const bool resolved = ResolveInMissingModule(symbol);
  const auto elapsed = std::chrono::steady_clock::now() - start;

  EXPECT_FALSE(resolved);
  // Timer granularity and truncation to whole milliseconds can end the
  // wait slightly before the deadline.
  EXPECT_GE(elapsed, kTimeout / 2);
  EXPECT_LT(elapsed, kMaxElapsed);
}

TEST_F(Addr2LineSymbolizeCallback, RejectsUnsuccessfulAddr2LineExit) {
  const ScopedFakeAddr2Line fake{ADDR2LINE_FAKE_FAIL_PATH};
  EXPECT_TRUE(InstallAddr2LineSymbolizeCallback());

  char symbol[kSymbolBufferSize] = {};
  EXPECT_FALSE(ResolveInMissingModule(symbol));
}

#ifndef NGLOG_OS_WINDOWS
TEST_F(Addr2LineSymbolizeCallback, KeepsExecutableAfterPathChanges) {
  const ScopedFakeAddr2Line fake{ADDR2LINE_FAKE_SLOW_PATH};
  DisableAddr2LineTimeout();
  EXPECT_TRUE(InstallAddr2LineSymbolizeCallback());

  EXPECT_TRUE(
      SetEnvironmentValue("PATH", "/nonexistent/directory/for/testing"));

  char symbol[kSymbolBufferSize] = {};
  EXPECT_TRUE(ResolveInMissingModule(symbol));
  EXPECT_STREQ("slow", symbol);
}
#endif  // NGLOG_OS_WINDOWS

TEST_F(Addr2LineSymbolizeCallback,
       SkipsGracefullyWhenAddr2LineProducesNoOutput) {
  const char* symbol = nullptr;
  {
    // Exits immediately without printing anything: distinct from addr2line
    // being entirely absent from PATH (no process is ever spawned) and from
    // it hanging (RunAddr2Line() reaches EOF here well within the timeout).
    const ScopedFakeAddr2Line fake{ADDR2LINE_FAKE_EMPTY_PATH};
    nglog::SetFlag(FLAGS_symbolize_line_info, true);
    EXPECT_TRUE(InstallAddr2LineSymbolizeCallback());

    symbol = TrySymbolizeTestFunction();
  }

  if (symbol == nullptr) {
    GTEST_SKIP() << kResolverUnavailable;
  }
  EXPECT_THAT(symbol, NamesFunctionWithoutLocation());
}

int main(int argc, char** argv) {
#ifdef NGLOG_OS_WINDOWS
  constexpr char kLongPathChildFlag[] = "--long-path-child";
  constexpr int kFirstArgument = 1;
  for (int index = kFirstArgument; index < argc; ++index) {
    if (std::strcmp(argv[index], kLongPathChildFlag) != 0) {
      continue;
    }
    g_long_path_child = true;
    for (int argument = index; argument + 1 < argc; ++argument) {
      argv[argument] = argv[argument + 1];
    }
    --argc;
    break;
  }
#endif
  nglog::SetFlag(FLAGS_logtostderr, true);
  InitializeLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);
  // Independent from whatever InitializeLogging() may have installed via
  // FLAGS_symbolize_line_info, so tests don't depend on flag defaults.
  InstallSymbolizeCallback(nullptr);
  return RUN_ALL_TESTS();
}
