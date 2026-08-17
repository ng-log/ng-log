// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//
// A dependency-free slow mailer used by logging_unittest.cc. It records that
// the mailer has started, then stays alive long enough to expose logging lock
// contention.

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

namespace {
constexpr std::chrono::seconds kMailerDelay{1};
constexpr char kMailerMarkerEnvironment[] = "NGLOG_TEST_MAILER_MARKER";
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
  } else {
    return EXIT_FAILURE;
  }
  std::this_thread::sleep_for(kMailerDelay);
  return 0;
}
