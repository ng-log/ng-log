<!--
SPDX-FileCopyrightText: 2026 The ng-log contributors

SPDX-License-Identifier: BSD-3-Clause
-->

# Migrating from glog

To migrate from glog to ng-log, you'll need to adjust your code as follows:

-   replace `<glog/logging.h>` with `<ng-log/logging.h>` and similarly for other heads (flags, log_severity, stl_logging, etc)
-   replace the `google::` namespace with `nglog::`
-   replace `InitGoogleLogging` with `InitializeLogging`
-   replace `IsGoogleLoggingInitialized` with `IsLoggingInitialized`
-   replace `ShutdownGoogleLogging` with `ShutdownLogging`
-   replace direct flag access such as `FLAGS_v = 2` with `nglog::SetFlag(FLAGS_v, 2)` and reads such as `FLAGS_v` with `nglog::GetFlag(FLAGS_v)`
-   replace gflags definitions such as `DEFINE_int32(name, value, help)` with `NGLOG_DEFINE(std::int32_t, name, value, help)` and declarations such as `DECLARE_int32(name)` with `NGLOG_DECLARE(std::int32_t, name)`, and use `nglog::DynamicFlag` for flags that are created and destroyed at run time
-   remove declarations of ng-log's own flags, such as `DECLARE_bool(logtostderr)`, because `ng-log/flags.h` already declares them
-   replace `gflags::ParseCommandLineFlags(&argc, &argv, true)` with `nglog::ParseCommandLineFlags(&argc, &argv, nglog::FlagParsingMode::kRemoveFlags)`, and pass `nglog::UnknownFlagMode::kIgnore` if another parser handles the remaining options
-   rename `GLOG_*` environment variables to `NGLOG_*`, e.g., `GLOG_v=1` to `NGLOG_v=1`
-   pass options with two dashes, since a single-dash argument such as `-v=1` is no longer parsed as an option
-   read `FLAGS_logfile_mode` as an `nglog::FileMode` and use `value()` to obtain the numeric mode, which is now written in octal on the command line and in the environment
