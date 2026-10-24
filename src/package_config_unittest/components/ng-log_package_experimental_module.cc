// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include <string>

import nglog.experimental;

namespace ngx = nglog::experimental;

// Names every entity the module exports, which fails to compile if the module
// omits one.
namespace exported {

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
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
using nglog::base::GetLogger;
using nglog::base::Logger;
using nglog::base::SetLogger;
using nglog::experimental::AddSink;
using nglog::experimental::Async;
using nglog::experimental::AsyncDelivery;
using nglog::experimental::Attribute;
using nglog::experimental::Attributes;
using nglog::experimental::AttributeSet;
using nglog::experimental::Comparison;
using nglog::experimental::Context;
using nglog::experimental::Debug;
using nglog::experimental::DebugOnly;
using nglog::experimental::DisableLogFile;
using nglog::experimental::DisableLogFileLink;
using nglog::experimental::Ensure;
using nglog::experimental::Eq;
using nglog::experimental::Errno;
using nglog::experimental::ErrnoDescription;
using nglog::experimental::Error;
using nglog::experimental::Every;
using nglog::experimental::FailureWriter;
using nglog::experimental::Fatal;
using nglog::experimental::FatalEscalation;
using nglog::experimental::FatalHandler;
using nglog::experimental::FatalInDebug;
using nglog::experimental::Field;
using nglog::experimental::FieldWriter;
using nglog::experimental::First;
using nglog::experimental::Flush;
using nglog::experimental::FormatString;
using nglog::experimental::Ge;
using nglog::experimental::Gt;
using nglog::experimental::Info;
using nglog::experimental::Initialize;
using nglog::experimental::IsDebugEnabled;
using nglog::experimental::IsEnabled;
using nglog::experimental::IsInitialized;
using nglog::experimental::Lazy;
using nglog::experimental::Le;
using nglog::experimental::Log;
using nglog::experimental::LogDirectories;
using nglog::experimental::LogFileExclusion;
using nglog::experimental::LogOnlyToStderr;
using nglog::experimental::Lt;
using nglog::experimental::Message;
using nglog::experimental::Multiple;
using nglog::experimental::Name;
using nglog::experimental::Ne;
using nglog::experimental::NotNull;
using nglog::experimental::NotNullCheck;
using nglog::experimental::Occurrence;
using nglog::experimental::OccurrenceArgument;
using nglog::experimental::OperandComparison;
using nglog::experimental::operator|;
using nglog::experimental::Record;
using nglog::experimental::RemoveSink;
using nglog::experimental::SetEmailThreshold;
using nglog::experimental::SetFailureWriter;
using nglog::experimental::SetFatalHandler;
using nglog::experimental::SetFingerprint;
using nglog::experimental::SetLogFile;
using nglog::experimental::SetLogFileLink;
using nglog::experimental::SetLogFileSuffix;
using nglog::experimental::SetStderrThreshold;
using nglog::experimental::SetVerbosity;
using nglog::experimental::Severity;
using nglog::experimental::Shutdown;
using nglog::experimental::SignalSafe;
using nglog::experimental::SignalSafeDestination;
using nglog::experimental::SignalSafeFormatString;
using nglog::experimental::Single;
using nglog::experimental::Sink;
using nglog::experimental::Site;
using nglog::experimental::SkipLogFile;
using nglog::experimental::StackTrace;
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

}  // namespace exported

int main(int /*argc*/, char** argv) {
  nglog::InitializeLogging(argv[0]);

  std::string text;
  ngx::Log(ngx::Warning | ngx::Tag{"disk"} | ngx::ToString{&text},
           "disk {} is {}% full", "sda", 90);

  const int usage = 90;
  ngx::Ensure(ngx::That(usage) <= 100, "usage {} exceeds 100%", usage);
  ngx::Ensure(ngx::NotNull(argv[0], "argv[0]"));

  nglog::ShutdownLogging();
}
