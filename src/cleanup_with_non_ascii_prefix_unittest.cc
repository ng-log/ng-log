// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause

#include <chrono>
#include <string>

#include "cleanup_test.h"

#ifdef NGLOG_OS_WINDOWS
#  include <windows.h>
#else
#  include <sys/stat.h>
#  include <sys/types.h>

#  include <cerrno>
#  include <cstring>
#endif

namespace {

// The directory name deliberately contains U+20AC encoded as UTF-8.
constexpr char kLogDir[] = "cleanup_\xE2\x82\xAC_dir";
#ifdef NGLOG_OS_WINDOWS
constexpr wchar_t kLogDirWide[] = L"cleanup_\u20AC_dir";
#endif

// The test runs repeatedly, so the directory may already exist.
void MakeLogDir() {
#ifdef NGLOG_OS_WINDOWS
  const BOOL created = CreateDirectoryW(kLogDirWide, nullptr);
  ASSERT_TRUE(created || GetLastError() == ERROR_ALREADY_EXISTS);
#else
  constexpr mode_t kDirectoryMode = 0777;
  ASSERT_TRUE(mkdir(kLogDir, kDirectoryMode) == 0 || errno == EEXIST)
      << std::strerror(errno);
#endif
}

}  // namespace

TEST(CleanImmediatelyWithNonAsciiPrefix, logging) {
  using namespace std::chrono_literals;
  constexpr unsigned kMessageCount = 1000;
  ASSERT_NO_FATAL_FAILURE(MakeLogDir());
  nglog::EnableLogCleaner(0h);
  nglog::SetLogFilenameExtension(".nonasciifoo");
  nglog::SetLogDestination(NGLOG_INFO,
                           (std::string(kLogDir) + "/test_cleanup_").c_str());

  for (unsigned i = 0; i < kMessageCount; ++i) {
    LOG(INFO) << nglog::kCleanupTestMessage;
  }

  nglog::DisableLogCleaner();
}

int main(int argc, char** argv) { return nglog::CleanupTestMain(argc, argv); }
