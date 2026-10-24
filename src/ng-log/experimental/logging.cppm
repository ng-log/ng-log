// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// The nglog.experimental module provides the declarations of
// ng-log/experimental/logging.h, including the setup functions and LogSink,
// to `import nglog.experimental;`. Modules cannot export macros, so
// NGLOG_COMPARE() requires including ng-log/experimental/ensure.h.
//
// The defaults of the template arguments that capture NGLOG_STRIP_LOG and the
// debug mode are fixed when the module interface is compiled rather than in
// each importing translation unit. Definitions that an importing translation
// unit makes, such as NGLOG_STRIP_LOG, therefore require including the headers
// instead.
//
// src/experimental/module_exports.jq generates the using-declarations below
// from the AST of the headers and describes how to regenerate them once the
// API changes.

module;

#include "ng-log/experimental/logging.h"
#include "ng-log/log_severity.h"

export module nglog.experimental;

export namespace nglog {

#ifdef NGLOG_OS_WINDOWS
using nglog::FormatWindowsMessage;
#endif

using nglog::AddLogSink;
using nglog::DisableLogCleaner;
using nglog::EnableLogCleaner;
using nglog::FlushLogFiles;
using nglog::FlushLogFilesUnsafe;
using nglog::GetLoggingDirectories;
using nglog::GetLogSeverityName;
using nglog::GetStackTrace;
using nglog::InitializeLogging;
using nglog::InstallFailureFunction;
using nglog::InstallFailureSignalHandler;
using nglog::InstallFailureWriter;
using nglog::InstallPrefixFormatter;
using nglog::IsFailureSignalHandlerInstalled;
using nglog::IsLoggingInitialized;
using nglog::logging_fail_func_t;
using nglog::LogMessage;
using nglog::LogMessageTime;
using nglog::LogSeverity;
using nglog::LogSink;
using nglog::LogToStderr;
using nglog::NUM_SEVERITIES;
using nglog::PrefixFormatterCallback;
using nglog::RemoveLogSink;
using nglog::ReprintFatalMessage;
using nglog::SendEmail;
using nglog::SetApplicationFingerprint;
using nglog::SetEmailLogging;
using nglog::SetLogDestination;
using nglog::SetLogFilenameExtension;
using nglog::SetLogSymlink;
using nglog::SetStderrLogging;
using nglog::ShutdownLogging;
using nglog::TruncateLogFile;
using nglog::TruncateStdoutStderr;

}  // namespace nglog

export namespace nglog::base {

using nglog::base::GetLogger;
using nglog::base::Logger;
using nglog::base::SetLogger;

}  // namespace nglog::base

export namespace nglog::experimental {

using nglog::experimental::Async;
using nglog::experimental::AsyncDelivery;
using nglog::experimental::Attribute;
using nglog::experimental::Attributes;
using nglog::experimental::AttributeSet;
using nglog::experimental::Comparison;
using nglog::experimental::Context;
using nglog::experimental::Debug;
using nglog::experimental::DebugOnly;
using nglog::experimental::Ensure;
using nglog::experimental::Eq;
using nglog::experimental::Errno;
using nglog::experimental::ErrnoDescription;
using nglog::experimental::Error;
using nglog::experimental::Every;
using nglog::experimental::Fatal;
using nglog::experimental::FatalEscalation;
using nglog::experimental::FatalInDebug;
using nglog::experimental::Field;
using nglog::experimental::FieldWriter;
using nglog::experimental::First;
using nglog::experimental::FormatString;
using nglog::experimental::Ge;
using nglog::experimental::Gt;
using nglog::experimental::Info;
using nglog::experimental::IsDebugEnabled;
using nglog::experimental::IsEnabled;
using nglog::experimental::Lazy;
using nglog::experimental::Le;
using nglog::experimental::Log;
using nglog::experimental::LogFileExclusion;
using nglog::experimental::Lt;
using nglog::experimental::Message;
using nglog::experimental::Multiple;
using nglog::experimental::Ne;
using nglog::experimental::NotNull;
using nglog::experimental::NotNullCheck;
using nglog::experimental::Occurrence;
using nglog::experimental::OccurrenceArgument;
using nglog::experimental::OperandComparison;
using nglog::experimental::operator|;
using nglog::experimental::Record;
using nglog::experimental::Severity;
using nglog::experimental::SignalSafe;
using nglog::experimental::SignalSafeDestination;
using nglog::experimental::SignalSafeFormatString;
using nglog::experimental::Single;
using nglog::experimental::Sink;
using nglog::experimental::Site;
using nglog::experimental::SkipLogFile;
using nglog::experimental::StrCaseEq;
using nglog::experimental::StrCaseNe;
using nglog::experimental::StrEq;
using nglog::experimental::StrNe;
using nglog::experimental::Syslog;
using nglog::experimental::SyslogDestination;
using nglog::experimental::Tag;
using nglog::experimental::That;
using nglog::experimental::ToSink;
using nglog::experimental::ToString;
using nglog::experimental::ToVector;
using nglog::experimental::Verbose;
using nglog::experimental::Warning;

}  // namespace nglog::experimental
