// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Structured sinks, which receive a message together with the fields of its
// attributes, such as for journald.

#ifndef NGLOG_EXPERIMENTAL_SINK_H
#define NGLOG_EXPERIMENTAL_SINK_H

#include "ng-log/experimental/requirements.h"  // IWYU pragma: keep

#if defined(NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_SUPPORTED)

#  include <chrono>
#  include <source_location>
#  include <span>
#  include <string_view>
#  include <version>

#  include "ng-log/log_severity.h"

namespace nglog::experimental {

// A field of a message, such as one an attribute contributes.
struct Field {
  std::string_view name;
  std::string_view value;
};

// A message as a structured sink receives it. It refers to data that is valid
// only while the sink receives it.
class Record {
 public:
  using Time = std::chrono::sys_time<std::chrono::system_clock::duration>;

  constexpr Record(LogSeverity severity, const std::source_location& location,
                   Time time, std::chrono::seconds utc_offset, bool utc,
                   const char* tag, std::string_view text,
                   std::span<const Field> fields) noexcept
      : severity_{severity},
        location_{location},
        time_{time},
        utc_offset_{utc_offset},
        utc_{utc},
        tag_{tag},
        text_{text},
        fields_{fields} {}

  constexpr LogSeverity severity() const noexcept { return severity_; }
  // Location of the logging call, including the function.
  constexpr const std::source_location& location() const noexcept {
    return location_;
  }
  // Time the message was logged. std::chrono::zoned_time converts it to a time
  // zone.
  constexpr Time time() const noexcept { return time_; }
  // Offset from UTC of the time zone the log files use for the message.
  constexpr std::chrono::seconds utc_offset() const noexcept {
    return utc_offset_;
  }
  // Whether the log files use UTC instead of the current time zone.
  constexpr bool utc() const noexcept { return utc_; }

#  if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
  // Time zone the log files use for the message. Throws std::runtime_error if
  // the time zone database is unavailable.
  const std::chrono::time_zone* time_zone() const {
    return utc_ ? std::chrono::locate_zone("UTC") : std::chrono::current_zone();
  }
  // Time the message was logged in the time zone of the log files.
  std::chrono::zoned_time<Time::duration> zoned_time() const {
    return std::chrono::zoned_time<Time::duration>{time_zone(), time_};
  }
#  endif
  // Tag of the message, or null for the short name of the program.
  constexpr const char* tag() const noexcept { return tag_; }
  // Formatted message without the prefix and the trailing newline.
  constexpr std::string_view text() const noexcept { return text_; }
  // Fields of the attributes in composition order.
  constexpr std::span<const Field> fields() const noexcept { return fields_; }

 private:
  LogSeverity severity_;
  std::source_location location_;
  Time time_;
  std::chrono::seconds utc_offset_;
  bool utc_;
  const char* tag_;
  std::string_view text_;
  std::span<const Field> fields_;
};

// Receives messages together with the fields of their attributes. ToSink()
// sends a message to it.
class Sink {
 public:
  virtual void Send(const Record& record) = 0;

 protected:
  ~Sink() = default;
};

}  // namespace nglog::experimental

#endif  // defined(NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_SUPPORTED)

#endif  // NGLOG_EXPERIMENTAL_SINK_H
