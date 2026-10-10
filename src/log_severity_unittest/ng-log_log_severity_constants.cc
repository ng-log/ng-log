// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include <ng-log/logging.h>

#include <type_traits>

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

int main() {
  // Must not compile
  LOG(0) << "type unsafe info";
  LOG(1) << "type unsafe info";
  LOG(2) << "type unsafe info";
  LOG(3) << "type unsafe info";
}
