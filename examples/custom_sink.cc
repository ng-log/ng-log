// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//

#include <ng-log/flags.h>
#include <ng-log/logging.h>

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <iterator>
#include <string>

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

void WriteUsage(nglog::HelpWriter& help) {
  help.Write(nglog::HelpTextStyle::kHeading, "Usage:");
  help.Write(" ");
  help.Write(nglog::HelpTextStyle::kExecutable, "custom_sink_example");
  help.Write(" ");
  help.Write(nglog::HelpTextStyle::kUsageOptions, "[options]");
  help.Write("\n\nWrites log messages to ");
  help.Write(nglog::HelpTextStyle::kStream, "stdout");
  help.Write(" through a custom sink. Use ");
  help.Write(nglog::HelpTextStyle::kOptionPrefix, "--");
  help.Write(nglog::HelpTextStyle::kOption, "logtostderr");
  help.Write("=");
  help.Write(nglog::HelpTextStyle::kConstant, "true");
  help.Write(" to route ng-log output to ");
  help.Write(nglog::HelpTextStyle::kStream, "stderr");
  help.Write(".\n");
}

}  // namespace

int main(int argc, char** argv) {
  nglog::SetUsageMessage(WriteUsage);
  nglog::ParseCommandLineFlags(&argc, &argv,
                               nglog::FlagParsingMode::kRemoveFlags);
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
