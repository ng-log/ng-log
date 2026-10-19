// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//
// A dependency-free mailer used by logging_unittest.cc. It records its
// arguments and consumes the message. It can then stay alive until the test
// releases it to expose logging lock contention.

#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {
constexpr char kMailerArgumentsEnvironment[] = "NGLOG_TEST_MAILER_ARGUMENTS";
constexpr char kMailerMarkerEnvironment[] = "NGLOG_TEST_MAILER_MARKER";
constexpr char kMailerReleaseEnvironment[] = "NGLOG_TEST_MAILER_RELEASE";
constexpr std::size_t kInputBufferSize = 4096;
constexpr std::chrono::milliseconds kReleasePollInterval{10};
// Exceeds the CTest timeout and only ends a mailer orphaned by a hung test.
constexpr std::chrono::minutes kReleaseTimeout{5};

// Writes each argument after the program name on a line of its own.
bool RecordArguments(const char* path, int argc, char** argv) {
  std::ofstream arguments{std::string{path}};
  for (int i = 1; i < argc; ++i) {
    arguments << argv[i] << '\n';
  }
  arguments.close();
  return !arguments.fail();
}

bool WaitForRelease(const char* path) {
  const auto deadline = std::chrono::steady_clock::now() + kReleaseTimeout;
  while (!std::ifstream{std::string{path}}.good()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(kReleasePollInterval);
  }
  return true;
}

bool ConsumeInput() {
  std::array<char, kInputBufferSize> buffer{};
  while (std::cin.read(buffer.data(),
                       static_cast<std::streamsize>(buffer.size())) ||
         std::cin.gcount() != 0) {
  }
  return !std::cin.bad();
}
}  // namespace

int main(int argc, char** argv) {
  const char* const arguments_path = std::getenv(kMailerArgumentsEnvironment);
  if (arguments_path != nullptr &&
      !RecordArguments(arguments_path, argc, argv)) {
    return EXIT_FAILURE;
  }
  const char* const marker_path = std::getenv(kMailerMarkerEnvironment);
  if (marker_path != nullptr) {
    std::ofstream marker{std::string{marker_path}};
    if (!marker) {
      return EXIT_FAILURE;
    }
    marker << "started\n";
    if (!marker) {
      return EXIT_FAILURE;
    }
    marker.close();
  }
  if (!ConsumeInput()) {
    return EXIT_FAILURE;
  }
  const char* const release_path = std::getenv(kMailerReleaseEnvironment);
  if (release_path != nullptr && !WaitForRelease(release_path)) {
    return EXIT_FAILURE;
  }
  return 0;
}
