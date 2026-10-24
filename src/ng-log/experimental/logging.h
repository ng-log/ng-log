// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Experimental logging functions based on std::format and std::source_location
// that are currently planned to fully supersede the logging macros in ng-log
// 0.10.0, after which the macros would be deprecated and eventually removed.
// The plan is informational and may change. The interface may change without
// notice and requires C++20.
//
// This header includes all experimental headers for convenience:
//
// - ng-log/experimental/attributes.h: the attribute protocol and the built-in
//   attributes
// - ng-log/experimental/format.h: the format string
// - ng-log/experimental/log_call.h: Log(), which the following two headers
//   enable for their destinations
// - ng-log/experimental/log.h: the regular destinations and IsEnabled()
// - ng-log/experimental/signal_safe_log.h: the SignalSafe destination
// - ng-log/experimental/ensure.h: Ensure(), the comparisons and NotNull()
// - ng-log/experimental/sink.h: Sink and the Record it receives
// - ng-log/experimental/setup.h: the setup functions, such as Initialize() and
//   SetLogFile()
//
// It also includes the headers of the setup functions of the logging macros and
// of LogSink. None of these headers defines unprefixed macros such as LOG or
// CHECK, so they can be combined with libraries that define the same names.
//
// For example:
//
//   namespace ngx = nglog::experimental;
//
//   ngx::Log(ngx::Warning, "disk {} is {}% full", name, usage);
//   ngx::Log(ngx::Error | ngx::Errno | ngx::Tag{"net"}, "connect to {}", host);
//   ngx::Ensure(ngx::That(fd) != -1, ngx::Errno, "open {}", path);
//   ngx::Log(ngx::SignalSafe | ngx::Error, "caught signal {}", signal);

#ifndef NGLOG_EXPERIMENTAL_LOGGING_H
#define NGLOG_EXPERIMENTAL_LOGGING_H

#include "ng-log/experimental/attributes.h"       // IWYU pragma: export
#include "ng-log/experimental/ensure.h"           // IWYU pragma: export
#include "ng-log/experimental/format.h"           // IWYU pragma: export
#include "ng-log/experimental/log.h"              // IWYU pragma: export
#include "ng-log/experimental/log_call.h"         // IWYU pragma: export
#include "ng-log/experimental/setup.h"            // IWYU pragma: export
#include "ng-log/experimental/signal_safe_log.h"  // IWYU pragma: export
#include "ng-log/experimental/sink.h"             // IWYU pragma: export
#include "ng-log/log_setup.h"                     // IWYU pragma: export
#include "ng-log/log_sink.h"                      // IWYU pragma: export

#endif  // NGLOG_EXPERIMENTAL_LOGGING_H
