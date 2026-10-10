// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//
// A dependency-free mailer used by logging_unittest.cc. It consumes the
// message before optionally staying alive long enough to expose logging lock
// contention.

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
constexpr std::size_t kInputBufferSize = 4096;

bool ConsumeInput() {
  std::array<char, kInputBufferSize> buffer{};
  while (std::cin.read(buffer.data(),
                       static_cast<std::streamsize>(buffer.size())) ||
         std::cin.gcount() != 0) {
  }
  return !std::cin.bad();
}
}  // namespace

int main() {
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
