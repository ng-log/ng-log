// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//
// A dependency-free mailer used by the tests. It consumes the message before
// optionally staying alive long enough to expose logging lock contention, and
// optionally records its arguments.

#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {
constexpr std::chrono::seconds kMailerDelay{1};
constexpr char kMailerMarkerEnvironment[] = "NGLOG_TEST_MAILER_MARKER";
constexpr char kMailerArgumentsEnvironment[] = "NGLOG_TEST_MAILER_ARGUMENTS";
constexpr std::size_t kInputBufferSize = 4096;

bool ConsumeInput() {
  std::array<char, kInputBufferSize> buffer{};
  while (std::cin.read(buffer.data(),
                       static_cast<std::streamsize>(buffer.size())) ||
         std::cin.gcount() != 0) {
  }
  return !std::cin.bad();
}

// Writes each argument after the program name on a line of its own.
bool RecordArguments(const char* path, int argc, char** argv) {
  std::ofstream arguments{std::string{path}};
  for (int i = 1; i < argc; ++i) {
    arguments << argv[i] << '\n';
  }
  return static_cast<bool>(arguments);
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
  if (marker_path != nullptr) {
    std::this_thread::sleep_for(kMailerDelay);
  }
  return 0;
}
