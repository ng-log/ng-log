#include "logging.h"

namespace google {

void InitGoogleLogging(const char* argv0) { nglog::InitializeLogging(argv0); }

bool IsGoogleLoggingInitialized() { return nglog::IsLoggingInitialized(); }

void ShutdownGoogleLogging() { nglog::ShutdownLogging(); }

void EnableLogCleaner(const std::chrono::minutes& overdue) {
  nglog::EnableLogCleaner(overdue);
}

void DisableLogCleaner() { nglog::DisableLogCleaner(); }

void SetApplicationFingerprint(const std::string& fingerprint) {
  nglog::SetApplicationFingerprint(fingerprint);
}

void FlushLogFiles(LogSeverity min_severity) {
  nglog::FlushLogFiles(min_severity);
}

void FlushLogFilesUnsafe(LogSeverity min_severity) {
  nglog::FlushLogFilesUnsafe(min_severity);
}

void SetLogDestination(LogSeverity severity, const char* base_filename) {
  nglog::SetLogDestination(severity, base_filename);
}

void SetLogSymlink(LogSeverity severity, const char* symlink_basename) {
  nglog::SetLogSymlink(severity, symlink_basename);
}

void AddLogSink(LogSink* destination) { nglog::AddLogSink(destination); }

void RemoveLogSink(LogSink* destination) { nglog::RemoveLogSink(destination); }

void InstallFailureSignalHandler() { nglog::InstallFailureSignalHandler(); }

bool IsFailureSignalHandlerInstalled() {
  return nglog::IsFailureSignalHandlerInstalled();
}

void InstallFailureWriter(void (*writer)(const char* data, size_t size)) {
  nglog::InstallFailureWriter(writer);
}

void SetLogFilenameExtension(const char* filename_extension) {
  nglog::SetLogFilenameExtension(filename_extension);
}

void SetStderrLogging(LogSeverity min_severity) {
  nglog::SetStderrLogging(min_severity);
}

void LogToStderr() { nglog::LogToStderr(); }

void SetEmailLogging(LogSeverity min_severity, const char* addresses) {
  nglog::SetEmailLogging(min_severity, addresses);
}

bool SendEmail(const char* dest, const char* subject, const char* body) {
  return nglog::SendEmail(dest, subject, body);
}

const std::vector<std::string>& GetLoggingDirectories() {
  return nglog::GetLoggingDirectories();
}

void ReprintFatalMessage() { return nglog::ReprintFatalMessage(); }

void TruncateLogFile(const char* path, uint64 limit, uint64 keep) {
  nglog::TruncateLogFile(path, limit, keep);
}

void TruncateStdoutStderr() { nglog::TruncateStdoutStderr(); }

const char* GetLogSeverityName(LogSeverity severity) {
  return nglog::GetLogSeverityName(severity);
}

}  // namespace google
