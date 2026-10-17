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

#ifndef _WIN32
#  include <csignal>
#endif

namespace {
// Bounds the wait for the release, so that a test that never releases the
// mailer does not leave it running.
constexpr std::chrono::seconds kReleaseTimeout{20};
constexpr std::chrono::milliseconds kReleasePollInterval{10};
constexpr char kMailerMarkerEnvironment[] = "NGLOG_TEST_MAILER_MARKER";
// When set, the mailer fails if it starts with SIGPIPE blocked, which would
// keep programs that it runs from stopping when a pipe closes.
constexpr char kMailerSignalsEnvironment[] = "NGLOG_TEST_MAILER_CHECK_SIGPIPE";
// When set, the mailer exits successfully without reading the message, like
// a mailer that hands nothing over to the mail system.
constexpr char kMailerIgnoreInputEnvironment[] =
    "NGLOG_TEST_MAILER_IGNORE_INPUT";
// When set, the mailer terminates itself with SIGTERM after reading the
// message.
constexpr char kMailerTerminateEnvironment[] = "NGLOG_TEST_MAILER_TERMINATE";
// When set, the mailer exits with the given status after reading the
// message.
constexpr char kMailerExitStatusEnvironment[] = "NGLOG_TEST_MAILER_EXIT_STATUS";
constexpr std::size_t kInputBufferSize = 4096;

bool ConsumeInput() {
  std::array<char, kInputBufferSize> buffer{};
  while (std::cin.read(buffer.data(),
                       static_cast<std::streamsize>(buffer.size())) ||
         std::cin.gcount() != 0) {
  }
  return !std::cin.bad();
}

bool SigpipeIsBlocked() {
#ifndef _WIN32
  sigset_t blocked;
  sigemptyset(&blocked);
  return sigprocmask(SIG_BLOCK, nullptr, &blocked) == 0 &&
         sigismember(&blocked, SIGPIPE) == 1;
#else
  return false;
#endif
}

}  // namespace

int main() {
  if (std::getenv(kMailerSignalsEnvironment) != nullptr && SigpipeIsBlocked()) {
    return EXIT_FAILURE;
  }
  if (std::getenv(kMailerIgnoreInputEnvironment) != nullptr) {
    return 0;
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
#ifndef _WIN32
  if (std::getenv(kMailerTerminateEnvironment) != nullptr) {
    std::raise(SIGTERM);
  }
#endif
  if (const char* const status = std::getenv(kMailerExitStatusEnvironment)) {
    return std::atoi(status);
  }
  if (marker_path != nullptr) {
    // Keeps running until the test releases it, then records that it is
    // about to exit, so that the test can tell whether other work finished
    // while the mailer was still running.
    const std::string base{marker_path};
    const auto deadline = std::chrono::steady_clock::now() + kReleaseTimeout;
    while (!std::ifstream{base + ".release"} &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(kReleasePollInterval);
    }
    std::ofstream exited{base + ".exited"};
    exited << "exited\n";
  }
  return 0;
}
