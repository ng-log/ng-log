// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//

#include <ng-log/logging.h>

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <iterator>

namespace {

struct MyLogSink : nglog::LogSink {  // (1)!
  void send(nglog::LogSeverity severity, const char* /*full_filename*/,
            const char* base_filename, int line,
            const nglog::LogMessageTime& /*time*/, const char* message,
            std::size_t message_len) override {
    std::cout << nglog::GetLogSeverityName(severity) << ' ' << base_filename
              << ':' << line << ' ';
    std::copy_n(message, message_len,
                std::ostreambuf_iterator<char>{std::cout});
    std::cout << '\n';
  }
};

}  // namespace

int main(int /*argc*/, char** argv) {
  nglog::InitializeLogging(argv[0]);

  MyLogSink sink;
  nglog::AddLogSink(&sink);  // (2)!

  LOG(INFO) << "logging to MySink";

  nglog::RemoveLogSink(&sink);  // (3)!

  // We can directly log to a sink without registering it
  LOG_TO_SINK(&sink, INFO) << "direct logging";  // (4)!
  LOG_TO_SINK_BUT_NOT_TO_LOGFILE(&sink, INFO)
      << "direct logging but not to file";
}
