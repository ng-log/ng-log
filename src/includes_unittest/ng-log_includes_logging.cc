// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include <ng-log/logging.h>

#include <type_traits>

// Checked here rather than in the tests that must fail to compile, where a
// failing assertion would go unnoticed.
static_assert(
    std::is_same<typename std::remove_const<decltype(NGLOG_INFO)>::type,
                 nglog::LogSeverity>::value,
    "NGLOG_INFO must have type nglog::LogSeverity");
static_assert(
    std::is_same<typename std::remove_const<decltype(NGLOG_WARNING)>::type,
                 nglog::LogSeverity>::value,
    "NGLOG_WARNING must have type nglog::LogSeverity");
static_assert(
    std::is_same<typename std::remove_const<decltype(NGLOG_ERROR)>::type,
                 nglog::LogSeverity>::value,
    "NGLOG_ERROR must have type nglog::LogSeverity");
static_assert(
    std::is_same<typename std::remove_const<decltype(NGLOG_FATAL)>::type,
                 nglog::LogSeverity>::value,
    "NGLOG_FATAL must have type nglog::LogSeverity");

static_assert(NGLOG_INFO == static_cast<nglog::LogSeverity>(0),
              "NGLOG_INFO must have value 0");
static_assert(NGLOG_WARNING == static_cast<nglog::LogSeverity>(1),
              "NGLOG_WARNING must have value 1");
static_assert(NGLOG_ERROR == static_cast<nglog::LogSeverity>(2),
              "NGLOG_ERROR must have value 2");
static_assert(NGLOG_FATAL == static_cast<nglog::LogSeverity>(3),
              "NGLOG_FATAL must have value 3");

#ifndef NGLOG_NO_ABBREVIATED_SEVERITIES
static_assert(nglog::INFO == NGLOG_INFO, "INFO must equal NGLOG_INFO");
static_assert(nglog::WARNING == NGLOG_WARNING,
              "WARNING must equal NGLOG_WARNING");
static_assert(nglog::ERROR == NGLOG_ERROR, "ERROR must equal NGLOG_ERROR");
static_assert(nglog::FATAL == NGLOG_FATAL, "FATAL must equal NGLOG_FATAL");
#endif

namespace {

// Occasional log statements form the body of an if statement without braces
// and without provoking warnings about an ambiguous else.
void LogOccasionallyWithoutBraces(bool condition) {
  if (condition) LOG_EVERY_N(INFO, 2) << "every n";
  if (condition) LOG_IF_EVERY_N(INFO, condition, 2) << "if every n";
  if (condition) LOG_FIRST_N(INFO, 2) << "first n";
  if (condition) LOG_EVERY_T(INFO, 1) << "every t";
  if (condition) PLOG_EVERY_N(INFO, 2) << "plog every n";
  if (condition) SYSLOG_EVERY_N(INFO, 2) << "syslog every n";
}

}  // namespace

int main() {
  LogOccasionallyWithoutBraces(true);
  LOG(INFO) << "info";
  LOG(WARNING) << "warning";
  LOG(ERROR) << "error";
  LOG(FATAL) << "fatal";
}
