// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//
// Unit tests for functions in addr2line.cc.

#include "addr2line.h"

#include <gtest/gtest.h>

#include "ng-log/platform.h"

#ifdef NGLOG_OS_WINDOWS
#  include <windows.h>
#else
#  include <sys/stat.h>
#  include <unistd.h>
#endif  // NGLOG_OS_WINDOWS

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

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

namespace {

void SetPathEnvironmentVariable(const char* value) {
#ifdef NGLOG_OS_WINDOWS
  _putenv_s("PATH", value != nullptr ? value : "");
#else
  if (value != nullptr) {
    setenv("PATH", value, 1);
  } else {
    unsetenv("PATH");
  }
#endif  // NGLOG_OS_WINDOWS
}

// Saves and restores the PATH environment variable so that tests which
// point it at a scratch directory don't leak that into other tests.
class ScopedPathOverride {
 public:
  explicit ScopedPathOverride(const char* value) {
    const char* current = std::getenv("PATH");
    had_previous_value_ = current != nullptr;
    if (had_previous_value_) {
      previous_value_ = current;
    }
    SetPathEnvironmentVariable(value);
  }

  ScopedPathOverride(const ScopedPathOverride&) = delete;
  ScopedPathOverride& operator=(const ScopedPathOverride&) = delete;

  ~ScopedPathOverride() {
    SetPathEnvironmentVariable(had_previous_value_ ? previous_value_.c_str()
                                                   : nullptr);
  }

 private:
  bool had_previous_value_;
  std::string previous_value_;
};

using UniqueFile = std::unique_ptr<std::FILE, int (*)(std::FILE*)>;

// Creates a fresh, empty scratch directory and returns its path.
// CHECK-fails on error.
std::string MakeScratchDirectory() {
#ifdef NGLOG_OS_WINDOWS
  char temp_path[MAX_PATH];
  CHECK_NE(0U, GetTempPathA(sizeof(temp_path), temp_path));
  char dir[MAX_PATH];
  // GetTempFileNameA() is used only to obtain a name that is guaranteed
  // unique within |temp_path|. The file it creates is immediately
  // replaced with a directory of the same name, which is the standard
  // way to reserve a unique temporary directory name on Windows.
  CHECK_NE(0U, GetTempFileNameA(temp_path, "a2l", 0, dir));
  CHECK(DeleteFileA(dir));
  CHECK(CreateDirectoryA(dir, nullptr));
  return dir;
#else
  char dir_template[] = "/tmp/addr2line_unittest.XXXXXX";
  const char* dir = mkdtemp(dir_template);
  CHECK(dir != nullptr);
  return dir;
#endif  // NGLOG_OS_WINDOWS
}

void RemoveScratchDirectory(const std::string& dir) {
#ifdef NGLOG_OS_WINDOWS
  RemoveDirectoryA(dir.c_str());
#else
  rmdir(dir.c_str());
#endif  // NGLOG_OS_WINDOWS
}

// Materializes a fake "addr2line" executable in |dir| and returns its
// path. |posix_script| is used verbatim as a POSIX shell script. A raw
// script cannot run via CreateProcess() on Windows, so
// |windows_helper_path| (a companion program built alongside this test,
// already exhibiting the needed behavior) is copied into place instead.
std::string MakeFakeAddr2Line(const std::string& dir, const char* posix_script,
                              const char* windows_helper_path) {
#ifdef NGLOG_OS_WINDOWS
  static_cast<void>(posix_script);
  // CreateProcess() appends ".exe" automatically to an extension-less
  // name, matching how the production code spawns "addr2line".
  const std::string path = dir + "\\addr2line.exe";
  CHECK(CopyFileA(windows_helper_path, path.c_str(), FALSE));
  return path;
#else
  static_cast<void>(windows_helper_path);
  const std::string path = dir + "/addr2line";
  UniqueFile f(std::fopen(path.c_str(), "w"), &std::fclose);
  CHECK(f != nullptr);
  std::fputs(posix_script, f.get());
  f.reset();
  CHECK_EQ(0, chmod(path.c_str(), 0755));
  return path;
#endif  // NGLOG_OS_WINDOWS
}

void RemoveFakeAddr2Line(const std::string& path) {
#ifdef NGLOG_OS_WINDOWS
  DeleteFileA(path.c_str());
#else
  unlink(path.c_str());
#endif  // NGLOG_OS_WINDOWS
}

}  // namespace

// Tests for FormatAddr2LineOutput.

TEST(FormatAddr2LineOutput, ResolvedLocation) {
  constexpr char kInput[] = "src/addr2line.cc:42\n";
  char out[64];
  const std::size_t written =
      FormatAddr2LineOutput(kInput, std::strlen(kInput), out, sizeof(out));
  EXPECT_EQ(std::strlen("src/addr2line.cc:42 "), written);
  EXPECT_STREQ("src/addr2line.cc:42 ", out);
}

TEST(FormatAddr2LineOutput, IgnoresTrailingDiscriminatorAnnotation) {
  // GNU addr2line appends " (discriminator N)" when an address maps to one
  // of several DWARF line-table discriminators for the same source line, as
  // commonly happens right at a CHECK/LOG(FATAL) call site.
  constexpr char kInput[] = "src/addr2line.cc:42 (discriminator 3)\n";
  char out[64];
  const std::size_t written =
      FormatAddr2LineOutput(kInput, std::strlen(kInput), out, sizeof(out));
  EXPECT_EQ(std::strlen("src/addr2line.cc:42 "), written);
  EXPECT_STREQ("src/addr2line.cc:42 ", out);
}

TEST(FormatAddr2LineOutput, IgnoresLinesAfterTheFirst) {
  constexpr char kInput[] = "src/addr2line.cc:42\nsrc/other.cc:1\n";
  char out[64];
  const std::size_t written =
      FormatAddr2LineOutput(kInput, std::strlen(kInput), out, sizeof(out));
  EXPECT_STREQ("src/addr2line.cc:42 ", out);
  EXPECT_EQ(std::strlen("src/addr2line.cc:42 "), written);
}

TEST(FormatAddr2LineOutput, RejectsUnresolvedLocation) {
  constexpr char kInput[] = "??:0\n";
  char out[64];
  EXPECT_EQ(
      0U, FormatAddr2LineOutput(kInput, std::strlen(kInput), out, sizeof(out)));
}

TEST(FormatAddr2LineOutput, RejectsMissingColon) {
  constexpr char kInput[] = "not-a-location\n";
  char out[64];
  EXPECT_EQ(
      0U, FormatAddr2LineOutput(kInput, std::strlen(kInput), out, sizeof(out)));
}

TEST(FormatAddr2LineOutput, RejectsNonNumericLine) {
  constexpr char kInput[] = "src/addr2line.cc:abc\n";
  char out[64];
  EXPECT_EQ(
      0U, FormatAddr2LineOutput(kInput, std::strlen(kInput), out, sizeof(out)));
}

TEST(FormatAddr2LineOutput, RejectsEmptyInput) {
  char out[64];
  EXPECT_EQ(0U, FormatAddr2LineOutput("", 0, out, sizeof(out)));
}

TEST(FormatAddr2LineOutput, RejectsWhenOutputDoesNotFit) {
  constexpr char kInput[] = "src/addr2line.cc:42\n";
  char out[8];  // Too small for "src/addr2line.cc:42 " + '\0'.
  EXPECT_EQ(
      0U, FormatAddr2LineOutput(kInput, std::strlen(kInput), out, sizeof(out)));
}

TEST(FormatAddr2LineOutput, RejectsEmptyFileField) {
  constexpr char kInput[] = ":42\n";
  char out[64];
  EXPECT_EQ(
      0U, FormatAddr2LineOutput(kInput, std::strlen(kInput), out, sizeof(out)));
}

TEST(FormatAddr2LineOutput, RejectsEmptyLineField) {
  constexpr char kInput[] = "src/addr2line.cc:\n";
  char out[64];
  EXPECT_EQ(
      0U, FormatAddr2LineOutput(kInput, std::strlen(kInput), out, sizeof(out)));
}

TEST(FormatAddr2LineOutput, HandlesInputWithoutTrailingNewline) {
  constexpr char kInput[] = "src/addr2line.cc:42";  // No trailing '\n'.
  char out[64];
  const std::size_t written =
      FormatAddr2LineOutput(kInput, std::strlen(kInput), out, sizeof(out));
  EXPECT_EQ(std::strlen("src/addr2line.cc:42 "), written);
  EXPECT_STREQ("src/addr2line.cc:42 ", out);
}

TEST(FormatAddr2LineOutput, HandlesColonWithinTheFilePath) {
  constexpr char kInput[] = "weird:name.cc:42\n";
  char out[64];
  const std::size_t written =
      FormatAddr2LineOutput(kInput, std::strlen(kInput), out, sizeof(out));
  EXPECT_EQ(std::strlen("weird:name.cc:42 "), written);
  EXPECT_STREQ("weird:name.cc:42 ", out);
}

TEST(FormatAddr2LineOutput, RejectsHighBitSetByteInLineField) {
  // A byte with the high bit set is never a valid line-number digit.
  // This also exercises that classifying it does not read out of bounds.
  constexpr char kInput[] = "src/addr2line.cc:\xff\n";
  char out[64];
  EXPECT_EQ(
      0U, FormatAddr2LineOutput(kInput, std::strlen(kInput), out, sizeof(out)));
}

TEST(FormatAddr2LineOutput, SucceedsWhenOutputExactlyFits) {
  constexpr char kInput[] = "src/addr2line.cc:42\n";
  constexpr char kExpected[] = "src/addr2line.cc:42 ";
  char out[64];
  const std::size_t exact_size = std::strlen(kExpected) + 1;  // +1 for NUL.
  const std::size_t written =
      FormatAddr2LineOutput(kInput, std::strlen(kInput), out, exact_size);
  EXPECT_EQ(std::strlen(kExpected), written);
  EXPECT_STREQ(kExpected, out);
}

TEST(FormatAddr2LineOutput, RejectsWhenOutputIsOneByteTooSmall) {
  constexpr char kInput[] = "src/addr2line.cc:42\n";
  constexpr char kExpected[] = "src/addr2line.cc:42 ";
  char out[64];
  const std::size_t one_byte_too_few = std::strlen(kExpected);  // No NUL.
  EXPECT_EQ(0U, FormatAddr2LineOutput(kInput, std::strlen(kInput), out,
                                      one_byte_too_few));
}

TEST(FormatAddr2LineOutput, RejectsZeroSizedOutputBuffer) {
  constexpr char kInput[] = "src/addr2line.cc:42\n";
  char out[64];
  EXPECT_EQ(0U, FormatAddr2LineOutput(kInput, std::strlen(kInput), out, 0));
}

TEST(FormatAddr2LineOutput, RejectsOutputTooSmallForTheLiteralBytes) {
  constexpr char kInput[] = "src/addr2line.cc:42\n";
  char out[64];
  // Room for a NUL and a byte or two of content, but not even the fixed
  // ':', ' ' that always separate the file and line fields.
  EXPECT_EQ(0U, FormatAddr2LineOutput(kInput, std::strlen(kInput), out, 2));
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

TEST(SplitAddr2LineFunctionsOutput, RejectsUnresolvedName) {
  constexpr char kInput[] = "??\n??:0\n";
  Addr2LineFunctionsResult result;
  EXPECT_FALSE(
      SplitAddr2LineFunctionsOutput(kInput, std::strlen(kInput), &result));
}

TEST(SplitAddr2LineFunctionsOutput, RejectsUnresolvedNameWithCrlfLineEndings) {
  // addr2line's stdout is opened in text mode on Windows, so its CRT
  // writes CRLF line endings. The "??" placeholder must still be
  // recognized with the trailing '\r' included in the line.
  constexpr char kInput[] = "??\r\n??:0\r\n";
  Addr2LineFunctionsResult result;
  EXPECT_FALSE(
      SplitAddr2LineFunctionsOutput(kInput, std::strlen(kInput), &result));
}

TEST(SplitAddr2LineFunctionsOutput, ResolvesNameWithCrlfLineEndings) {
  constexpr char kInput[] = "main\r\nsrc/addr2line.cc:42\r\n";
  Addr2LineFunctionsResult result;
  ASSERT_TRUE(
      SplitAddr2LineFunctionsOutput(kInput, std::strlen(kInput), &result));
  EXPECT_EQ(std::string(result.name, result.name_len), "main");
}

TEST(SplitAddr2LineFunctionsOutput, RejectsMissingSecondLine) {
  constexpr char kInput[] = "main";  // No '\n' anywhere.
  Addr2LineFunctionsResult result;
  EXPECT_FALSE(
      SplitAddr2LineFunctionsOutput(kInput, std::strlen(kInput), &result));
}

TEST(SplitAddr2LineFunctionsOutput, RejectsEmptyNameField) {
  constexpr char kInput[] = "\nsrc/addr2line.cc:42\n";
  Addr2LineFunctionsResult result;
  EXPECT_FALSE(
      SplitAddr2LineFunctionsOutput(kInput, std::strlen(kInput), &result));
}

// Tests for Addr2LineSymbolizeCallback, exercised through Symbolize().
// This whole file only builds when HAVE_ADDR2LINE is defined, which
// always implies HAVE_SYMBOLIZE on every platform this backend
// supports, so Symbolize() below is always the real implementation,
// never a stub.

namespace {

void ATTRIBUTE_NOINLINE FunctionSymbolizedForAddr2LineTest() {
  volatile int a = 0;
  a = a + 1;
}

const char* TrySymbolize(void* pc) {
  static char symbol[4096];
  if (Symbolize(pc, symbol, sizeof(symbol))) {
    return symbol;
  }
  return nullptr;
}

}  // namespace

// On platforms where addr2line supplements a name resolver that always
// succeeds on its own (the ELF reader on Linux), symbol is never null.
// On the Windows backend, addr2line resolves the name itself, so when it
// cannot (unavailable, or the known same-translation-unit-as-main()
// linker quirk noted in symbolize.cc), Symbolize() legitimately fails
// outright. The tests below treat a null symbol as a reason to skip
// rather than a hard failure.

TEST(Addr2LineSymbolizeCallback, ResolvesFileAndLine) {
  FLAGS_symbolize_line_info = true;
  EXPECT_TRUE(InstallAddr2LineSymbolizeCallback());

  const char* symbol = TrySymbolize(
      reinterpret_cast<void*>(&FunctionSymbolizedForAddr2LineTest));
  InstallSymbolizeCallback(nullptr);

  if (symbol == nullptr ||
      std::strstr(symbol, "addr2line_unittest.cc:") == nullptr) {
    GTEST_SKIP() << "addr2line not available or not resolving";
  }
  EXPECT_NE(nullptr, std::strstr(symbol, "FunctionSymbolizedForAddr2LineTest"));
}

TEST(Addr2LineSymbolizeCallback, DisabledByFlag) {
  FLAGS_symbolize_line_info = false;
  EXPECT_TRUE(InstallAddr2LineSymbolizeCallback());

  const char* symbol = TrySymbolize(
      reinterpret_cast<void*>(&FunctionSymbolizedForAddr2LineTest));
  FLAGS_symbolize_line_info = true;
  InstallSymbolizeCallback(nullptr);

  if (symbol == nullptr ||
      std::strstr(symbol, "FunctionSymbolizedForAddr2LineTest") == nullptr) {
    GTEST_SKIP() << "addr2line not available or not resolving";
  }
  EXPECT_EQ(nullptr, std::strstr(symbol, "addr2line_unittest.cc:"));
}

TEST(Addr2LineSymbolizeCallback, SkipsGracefullyWhenUnavailable) {
  ScopedPathOverride scoped_path("/nonexistent/directory/for/testing");

  FLAGS_symbolize_line_info = true;
  EXPECT_TRUE(InstallAddr2LineSymbolizeCallback());

  const char* symbol = TrySymbolize(
      reinterpret_cast<void*>(&FunctionSymbolizedForAddr2LineTest));
  InstallSymbolizeCallback(nullptr);

  if (symbol == nullptr) {
    GTEST_SKIP() << "no name resolver available without addr2line";
  }
  EXPECT_EQ(nullptr, std::strstr(symbol, "addr2line_unittest.cc:"));
  EXPECT_NE(nullptr, std::strstr(symbol, "FunctionSymbolizedForAddr2LineTest"));
}

TEST(Addr2LineSymbolizeCallback, DoesNotHangOnAnUnresponsiveAddr2Line) {
  const std::string dir = MakeScratchDirectory();
  // The POSIX script busy-loops using only the shell's own ":" builtin,
  // avoiding any dependency on external commands or an open stdin.
  // Neither this nor its Windows counterpart ever writes anything,
  // exercising the timeout / forced-termination path in RunAddr2Line().
  const std::string fake_executable = MakeFakeAddr2Line(
      dir, "#!/bin/sh\nwhile :; do :; done\n", ADDR2LINE_FAKE_HANG_PATH);

  const char* symbol = nullptr;
  {
    ScopedPathOverride scoped_path(dir.c_str());

    // Short enough to keep this test fast, while still giving the
    // busy-loop script above plenty of time to be seen as unresponsive.
    const nglog::int32 previous_timeout_ms = FLAGS_addr2line_timeout_ms;
    FLAGS_addr2line_timeout_ms = 50;
    FLAGS_symbolize_line_info = true;
    EXPECT_TRUE(InstallAddr2LineSymbolizeCallback());

    symbol = TrySymbolize(
        reinterpret_cast<void*>(&FunctionSymbolizedForAddr2LineTest));

    InstallSymbolizeCallback(nullptr);
    FLAGS_addr2line_timeout_ms = previous_timeout_ms;
  }

  RemoveFakeAddr2Line(fake_executable);
  RemoveScratchDirectory(dir);

  if (symbol == nullptr) {
    GTEST_SKIP() << "no name resolver available without addr2line";
  }
  EXPECT_EQ(nullptr, std::strstr(symbol, "addr2line_unittest.cc:"));
  EXPECT_NE(nullptr, std::strstr(symbol, "FunctionSymbolizedForAddr2LineTest"));
}

TEST(Addr2LineSymbolizeCallback, UsesOneTimeoutForSlowOutput) {
  const std::string dir = MakeScratchDirectory();
  const std::string fake_executable = MakeFakeAddr2Line(
      dir,
      "#!/bin/sh\n"
      "printf s; sleep 0.02; printf l; sleep 0.02; printf o; sleep 0.02; "
      "printf w; sleep 0.02; printf '\\n'; sleep 0.02; printf '?'; "
      "sleep 0.02; printf '?'; sleep 0.02; printf ':'; sleep 0.02; "
      "printf '0\\n';",
      ADDR2LINE_FAKE_SLOW_PATH);

  {
    ScopedPathOverride scoped_path(dir.c_str());
    const nglog::int32 previous_timeout_ms = FLAGS_addr2line_timeout_ms;
    FLAGS_addr2line_timeout_ms = 40;

    char symbol[4096];
    const auto start = std::chrono::steady_clock::now();
    const bool resolved = ResolveFunctionAndLine(
        "missing.exe", reinterpret_cast<void*>(1), 0, symbol, sizeof(symbol),
        SymbolizeOptions::kNone, nullptr);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    FLAGS_addr2line_timeout_ms = previous_timeout_ms;
    EXPECT_FALSE(resolved);
    EXPECT_LT(elapsed, std::chrono::milliseconds{160});
  }

  RemoveFakeAddr2Line(fake_executable);
  RemoveScratchDirectory(dir);
}

TEST(Addr2LineSymbolizeCallback, SkipsGracefullyWhenAddr2LineProducesNoOutput) {
  const std::string dir = MakeScratchDirectory();
  // Exits immediately without printing anything: distinct from addr2line
  // being entirely absent from PATH (no process is ever spawned) and from
  // it hanging (RunAddr2Line() reaches EOF here well within the timeout).
  const std::string fake_executable =
      MakeFakeAddr2Line(dir, "#!/bin/sh\nexit 0\n", ADDR2LINE_FAKE_EMPTY_PATH);

  const char* symbol = nullptr;
  {
    ScopedPathOverride scoped_path(dir.c_str());

    FLAGS_symbolize_line_info = true;
    EXPECT_TRUE(InstallAddr2LineSymbolizeCallback());

    symbol = TrySymbolize(
        reinterpret_cast<void*>(&FunctionSymbolizedForAddr2LineTest));

    InstallSymbolizeCallback(nullptr);
  }

  RemoveFakeAddr2Line(fake_executable);
  RemoveScratchDirectory(dir);

  if (symbol == nullptr) {
    GTEST_SKIP() << "no name resolver available without addr2line";
  }
  EXPECT_EQ(nullptr, std::strstr(symbol, "addr2line_unittest.cc:"));
  EXPECT_NE(nullptr, std::strstr(symbol, "FunctionSymbolizedForAddr2LineTest"));
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
