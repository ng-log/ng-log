// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "internal/log_cleaner.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "config.h"
#include "internal/character_classification.h"
#include "utilities.h"

#ifdef NGLOG_OS_WINDOWS
#  include "windows/dirent.h"
#else
#  include <dirent.h>
#endif

#include <sys/stat.h>

#if defined(HAVE_FORK) && defined(HAVE_PTHREAD_ATFORK)
#  include <pthread.h>
#endif

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

namespace nglog {
namespace internal {
namespace {

constexpr std::int32_t kMinimumCleanupIntervalSeconds = 1;
constexpr std::size_t kDateLength = 8;
constexpr std::size_t kTimeLength = 6;
constexpr std::size_t kDateSeparatorOffset = kDateLength;
constexpr std::size_t kTimestampSeparatorOffset =
    kDateSeparatorOffset + 1 + kTimeLength;
constexpr std::size_t kTimestampAndPidSeparatorLength =
    kTimestampSeparatorOffset + 1;
constexpr std::size_t kMinimumPidDigits = 1;

#ifdef NGLOG_OS_WINDOWS
const char possible_dir_delim[] = {'\\', '/'};

// Device and inode-equivalent, packed the same way GetFileInformation() does.
void ExtractIdentity(const BY_HANDLE_FILE_INFORMATION& file_information,
                     std::uintmax_t* device, std::uintmax_t* inode) {
  constexpr unsigned kDwordBitCount = sizeof(DWORD) * CHAR_BIT;
  *device = file_information.dwVolumeSerialNumber;
  *inode = (static_cast<std::uintmax_t>(file_information.nFileIndexHigh)
            << kDwordBitCount) |
           file_information.nFileIndexLow;
}

// perror()-equivalent for the GetLastError() Win32 APIs use instead of errno.
void ReportLastError(const std::string& context) {
  constexpr DWORD kMinimumMessageChars = 100;
  const DWORD error_code = GetLastError();
  LPSTR message = nullptr;
  const DWORD message_length = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, error_code, 0, reinterpret_cast<LPSTR>(&message),
      kMinimumMessageChars, nullptr);
  std::unique_ptr<char, decltype(&LocalFree)> release{message, &LocalFree};
  if (message_length > 0) {
    fprintf(stderr, "%s: %s", context.c_str(), message);
  } else {
    fprintf(stderr, "%s: error 0x%08lx\n", context.c_str(), error_code);
  }
}
#else
const char possible_dir_delim[] = {'/'};
#endif

// Every live LogCleaner instance, so pthread_atfork() handlers can lock and
// reset all of them, not just g_log_cleaner. Function-local static storage
// avoids initialization-order and destruction-order dependencies with
// LogCleaner instances in other translation units.
struct LiveInstanceState {
  std::mutex mutex;
  std::vector<LogCleaner*> instances;
};

LiveInstanceState& GetLiveInstanceState() {
  static LiveInstanceState state;
  return state;
}

std::vector<LogCleaner*>& LiveInstances() {
  return GetLiveInstanceState().instances;
}

std::mutex& LiveInstancesMutex() { return GetLiveInstanceState().mutex; }

}  // namespace

NGLOG_NO_EXPORT LogCleaner g_log_cleaner;

LogCleaner::LogCleaner() {
  {
    std::lock_guard<std::mutex> l{LiveInstancesMutex()};
    LiveInstances().push_back(this);
  }
#if defined(HAVE_FORK) && defined(HAVE_PTHREAD_ATFORK)
  static std::once_flag atfork_registration;
  std::call_once(atfork_registration, [] {
    pthread_atfork(&LogCleaner::PrepareFork, &LogCleaner::ParentAfterFork,
                   &LogCleaner::ChildAfterFork);
  });
#endif
}

LogCleaner::~LogCleaner() {
  static_cast<void>(Stop());
  std::lock_guard<std::mutex> l{LiveInstancesMutex()};
  auto& instances = LiveInstances();
  instances.erase(std::remove(instances.begin(), instances.end(), this),
                  instances.end());
}

NGLOG_ATTRIBUTE_NOINLINE
bool LogCleaner::Stop() {
  std::unique_ptr<std::thread> cleaner;
  bool was_enabled;
  {
    std::lock_guard<CleanerMutex> l{mutex_};
    was_enabled = std::exchange(enabled_, false);
    cleaner = std::move(thread_);
  }
  if (cleaner != nullptr) {
    cond_.notify_one();
  }
  if (cleaner != nullptr && cleaner->joinable()) {
    cleaner->join();
  }
  return was_enabled;
}

void LogCleaner::Enable(const std::chrono::minutes& overdue) {
#if defined(HAVE_FORK) && !defined(HAVE_PTHREAD_ATFORK)
  // A cleaner thread cannot survive fork() safely without fork handlers.
  std::lock_guard<CleanerMutex> l{mutex_};
  in_child_after_fork_ = false;
  overdue_ = overdue;
  cleanup_interval_ = std::chrono::seconds{
      std::max(FLAGS_logcleansecs, kMinimumCleanupIntervalSeconds)};
  return;
#else
  {
    std::lock_guard<CleanerMutex> l{mutex_};
    in_child_after_fork_ = false;
    overdue_ = overdue;
    cleanup_interval_ = std::chrono::seconds{
        std::max(FLAGS_logcleansecs, kMinimumCleanupIntervalSeconds)};
    if (!std::exchange(enabled_, true)) {
      // Logs may have become overdue while the cleaner was disabled: make all
      // known patterns due for an immediate scan.
      for (auto& pattern : patterns_) {
        pattern.second.next_cleanup_time = {};
      }
      thread_ = std::make_unique<std::thread>(&LogCleaner::Worker, this);
    }
  }
  // Wake up the cleaner thread: the overdue window may have changed.
  cond_.notify_one();
#endif
}

// Keep the callbacks out of line so their addresses remain available to
// pthread_atfork() and direct tests.
NGLOG_ATTRIBUTE_NOINLINE
void LogCleaner::PrepareFork() {
  LiveInstancesMutex().lock();
  // Lock in a fixed order so a concurrent fork() can't deadlock against
  // another thread locking two instances differently.
  for (LogCleaner* instance : LiveInstances()) {
    instance->mutex_.lock();
  }
}

NGLOG_ATTRIBUTE_NOINLINE
void LogCleaner::ParentAfterFork() {
  const auto& instances = LiveInstances();
  for (auto it = instances.rbegin(); it != instances.rend(); ++it) {
    (*it)->mutex_.unlock();
  }
  LiveInstancesMutex().unlock();
}

NGLOG_ATTRIBUTE_NOINLINE
void LogCleaner::ChildAfterFork() {
  for (LogCleaner* instance : LiveInstances()) {
    instance->enabled_ = false;
    instance->in_child_after_fork_ = true;
    // release(), not reset(): the thread doesn't exist in the child, so
    // destroying it as joinable would call std::terminate(). Leaking the
    // small std::thread object here is the safe trade-off.
    instance->thread_.release();
    instance->mutex_.unlock();
  }
  LiveInstancesMutex().unlock();
}

void LogCleaner::Disable() {
  // Stop() reports whether the cleaner was enabled atomically, so a
  // concurrent Enable() can't be missed by reading enabled_ separately.
  const bool was_enabled = Stop();

  // Callers disable the cleaner right before exiting and expect cleaning to
  // have happened by the time Disable() returns, so run a final scan here
  // rather than racing the cleaner thread for it. Stop() has already joined
  // that thread, so this runs uncontended.
  std::unordered_map<std::string, LogFilePattern> patterns;
  std::chrono::minutes overdue;
  {
    std::lock_guard<CleanerMutex> l{mutex_};
    if (in_child_after_fork_) {
      return;
    }
    if (!was_enabled) {
      return;
    }
    patterns = patterns_;
    overdue = overdue_;
  }

  const auto current_time = std::chrono::system_clock::now();
  for (const auto& pattern : patterns) {
    CleanOverdueLogs(current_time, overdue,
                     pattern.second.base_filename_selected, pattern.first,
                     pattern.second.filename_extension);
  }
}

void LogCleaner::AddLogFilePattern(bool base_filename_selected,
                                   const std::string& base_filename,
                                   const std::string& filename_extension) {
  // The only caller, LogFileObject::Write(), already returns early when
  // base_filename_selected_ is true and base_filename_ is empty (that
  // combination means "don't write"), so this combination can never reach
  // here.
  assert(!base_filename_selected || !base_filename.empty());

  {
    std::lock_guard<internal::CleanerMutex> l{mutex_};
    LogFilePattern& pattern = patterns_[base_filename];
    pattern.base_filename_selected = base_filename_selected;
    pattern.filename_extension = filename_extension;
    // A log file was just created: scan for overdue logs right away.
    pattern.next_cleanup_time = {};
  }
  cond_.notify_one();
}

void LogCleaner::Worker() {
  for (;;) {
    // Scans are scheduled on the steady clock, so fetch it before taking the
    // lock: it does not depend on any guarded state.
    const auto now = std::chrono::steady_clock::now();

    std::vector<std::pair<std::string, LogFilePattern>> due;
    std::chrono::minutes overdue{};

    {
      std::unique_lock<CleanerMutex> l{mutex_};
      if (!enabled_) {
        return;
      }

      // Collect the patterns which are due for a scan and schedule their
      // next one.
      due.reserve(patterns_.size());
      for (auto& pattern : patterns_) {
        if (pattern.second.next_cleanup_time <= now) {
          due.emplace_back(pattern.first, pattern.second);
          pattern.second.next_cleanup_time =
              now +
              std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                  cleanup_interval_);
        }
      }

      if (!due.empty()) {
        overdue = overdue_;
      } else if (patterns_.empty()) {
        cond_.wait(l, [&enabled = enabled_, &patterns = patterns_] {
          return !enabled || !patterns.empty();
        });
        continue;
      } else {
        auto next_deadline = patterns_.begin()->second.next_cleanup_time;
        for (const auto& pattern : patterns_) {
          next_deadline =
              std::min(next_deadline, pattern.second.next_cleanup_time);
        }
        cond_.wait_until(l, next_deadline);
        continue;
      }
    }

    // Scan without the lock held: directory I/O is slow and must not block
    // threads which are creating log files. The wall-clock time is only
    // needed here, to decide how old a log file is, so it is fetched right
    // before use rather than unconditionally at the top of the loop.
    const auto current_time = std::chrono::system_clock::now();
    for (const auto& pattern : due) {
      CleanOverdueLogs(current_time, overdue,
                       pattern.second.base_filename_selected, pattern.first,
                       pattern.second.filename_extension);
    }
    // New patterns may have been registered or the cleaner may have been
    // stopped while the lock was released: loop back to re-check both.
  }
}

void LogCleaner::CleanOverdueLogs(
    const std::chrono::system_clock::time_point& current_time,
    const std::chrono::minutes& overdue, bool base_filename_selected,
    const std::string& base_filename, const std::string& filename_extension) {
  std::vector<std::string> dirs;

  if (!base_filename_selected) {
    dirs = GetLoggingDirectories();
  } else {
    std::size_t pos = base_filename.find_last_of(
        possible_dir_delim, std::string::npos, sizeof(possible_dir_delim));
    if (pos != std::string::npos) {
      std::string dir = base_filename.substr(0, pos + 1);
      dirs.push_back(dir);
    } else {
      dirs.emplace_back(".");
    }
  }

  for (const std::string& dir : dirs) {
    std::vector<OverdueLog> logs = GetOverdueLogNames(
        dir, current_time, overdue, base_filename, filename_extension);
    for (const OverdueLog& log : logs) {
      // Re-verifies identity before removing. On Windows this closes the
      // TOCTOU window POSIX unlink()-by-name can't avoid.
      RemoveLogIfUnchanged(log.filepath, log.identity.device,
                           log.identity.inode);
    }
  }
}

std::vector<LogCleaner::OverdueLog> LogCleaner::GetOverdueLogNames(
    std::string log_directory,
    const std::chrono::system_clock::time_point& current_time,
    const std::chrono::minutes& overdue, const std::string& base_filename,
    const std::string& filename_extension) {
  // The overdue logs.
  std::vector<OverdueLog> overdue_log_names;

  // Try to get all files within log_directory.
  DIR* dir;
  struct dirent* ent;

  if ((dir = opendir(log_directory.c_str()))) {
    while ((ent = readdir(dir))) {
      if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
        continue;
      }

      std::string filepath = ent->d_name;
      const char* const dir_delim_end =
          possible_dir_delim + sizeof(possible_dir_delim);

      if (!log_directory.empty() &&
          std::find(possible_dir_delim, dir_delim_end,
                    log_directory[log_directory.size() - 1]) != dir_delim_end) {
        filepath = log_directory + filepath;
      }

      FileIdentity identity;
      if (IsLogFromCurrentProject(filepath, base_filename,
                                  filename_extension) &&
          IsLogLastModifiedOver(filepath, current_time, overdue, &identity)) {
        overdue_log_names.push_back({filepath, identity});
      }
    }
    closedir(dir);
  }

  return overdue_log_names;
}

NGLOG_ATTRIBUTE_NOINLINE
bool LogCleaner::IsLogFromCurrentProject(
    const std::string& filepath, const std::string& base_filename,
    const std::string& filename_extension) {
  // We should remove duplicated delimiters from `base_filename`, e.g.,
  // before: "/tmp//<base_filename>.<create_time>.<pid>"
  // after:  "/tmp/<base_filename>.<create_time>.<pid>"
  std::string cleaned_base_filename;

  const char* const dir_delim_end =
      possible_dir_delim + sizeof(possible_dir_delim);

  std::size_t real_filepath_size = filepath.size();
  for (char c : base_filename) {
    if (cleaned_base_filename.empty()) {
      cleaned_base_filename += c;
    } else if (std::find(possible_dir_delim, dir_delim_end, c) ==
                   dir_delim_end ||
               (!cleaned_base_filename.empty() &&
                c != cleaned_base_filename[cleaned_base_filename.size() - 1])) {
      cleaned_base_filename += c;
    }
  }

  // Return early if the filename does not start with cleaned_base_filename.
  if (filepath.find(cleaned_base_filename) != 0) {
    return false;
  }

  // Check whether filename_extension is next to cleaned_base_filename in
  // filepath if the user has set a custom filename extension.
  if (!filename_extension.empty()) {
    if (cleaned_base_filename.size() >= real_filepath_size) {
      return false;
    }
    // For the original version, filename_extension is in the filepath middle.
    std::string ext = filepath.substr(cleaned_base_filename.size(),
                                      filename_extension.size());
    if (ext == filename_extension) {
      cleaned_base_filename += filename_extension;
    } else {
      // For the new version, filename_extension is at the filepath end.
      if (filename_extension.size() >= real_filepath_size) {
        return false;
      }
      real_filepath_size = filepath.size() - filename_extension.size();
      if (filepath.substr(real_filepath_size) != filename_extension) {
        return false;
      }
    }
  }

  const std::size_t timestamp_start = cleaned_base_filename.size();
  if (real_filepath_size <
      timestamp_start + kTimestampAndPidSeparatorLength + kMinimumPidDigits) {
    return false;
  }

  const std::size_t suffix_length = real_filepath_size - timestamp_start;
  // Use .data() instead of .cbegin() to avoid sign conversion warnings due to
  // timestamp_start being size_t and therefore incompatible to difference_type.
  const auto* timestamp_begin = filepath.data() + timestamp_start;
  const bool date_is_digits = std::all_of(
      timestamp_begin, timestamp_begin + kDateLength, IsDecimalDigit);
  const bool time_is_digits =
      std::all_of(timestamp_begin + kDateSeparatorOffset + 1,
                  timestamp_begin + kTimestampSeparatorOffset, IsDecimalDigit);
  const bool pid_is_digits =
      std::all_of(timestamp_begin + kTimestampAndPidSeparatorLength,
                  timestamp_begin + suffix_length, IsDecimalDigit);
  if (!date_is_digits || !time_is_digits || !pid_is_digits) {
    return false;
  }

  if (filepath[timestamp_start + kDateSeparatorOffset] != '-' ||
      filepath[timestamp_start + kTimestampSeparatorOffset] != '.') {
    return false;
  }

  return true;
}

NGLOG_ATTRIBUTE_NOINLINE
bool LogCleaner::IsLogLastModifiedOver(
    const std::string& filepath,
    const std::chrono::system_clock::time_point& current_time,
    const std::chrono::minutes& overdue, FileIdentity* identity) {
  FileInformation information;
  if (GetFileInformation(filepath, &information)) {
    const auto diff = current_time - information.last_modified;
    if (diff < overdue) {
      return false;
    }
    *identity = information.identity;
    return true;
  }

  // If failed to get file stat, do not return true.
  return false;
}

NGLOG_ATTRIBUTE_NOINLINE
bool LogCleaner::GetFileInformation(const std::string& filepath,
                                    FileInformation* information) {
#ifdef NGLOG_OS_WINDOWS
  const HANDLE file =
      CreateFileA(filepath.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }

  BY_HANDLE_FILE_INFORMATION file_information;
  const bool succeeded = GetFileInformationByHandle(file, &file_information);
  CloseHandle(file);
  if (!succeeded) {
    return false;
  }

  constexpr std::uint64_t kFileTimeTicksPerSecond = 10000000;
  constexpr std::int64_t kWindowsToUnixEpochSeconds = 11644473600;
  ULARGE_INTEGER last_write_time;
  last_write_time.LowPart = file_information.ftLastWriteTime.dwLowDateTime;
  last_write_time.HighPart = file_information.ftLastWriteTime.dwHighDateTime;
  const auto last_modified_seconds =
      static_cast<std::int64_t>(last_write_time.QuadPart /
                                kFileTimeTicksPerSecond) -
      kWindowsToUnixEpochSeconds;
  information->last_modified = std::chrono::system_clock::from_time_t(
      static_cast<std::time_t>(last_modified_seconds));

  // Not guaranteed unique/stable on all filesystems (e.g. FAT/exFAT), per
  // Microsoft's own docs, so the identity check may miss a replaced file.
  ExtractIdentity(file_information, &information->identity.device,
                  &information->identity.inode);
  return true;
#else
  struct stat file_stat;
  if (stat(filepath.c_str(), &file_stat) != 0) {
    return false;
  }
  information->last_modified =
      std::chrono::system_clock::from_time_t(file_stat.st_mtime);
  information->identity.device = static_cast<std::uintmax_t>(file_stat.st_dev);
  information->identity.inode = static_cast<std::uintmax_t>(file_stat.st_ino);
  return true;
#endif
}

NGLOG_ATTRIBUTE_NOINLINE
bool LogCleaner::GetFileIdentity(const std::string& filepath,
                                 FileIdentity* identity) {
  FileInformation information;
  if (!GetFileInformation(filepath, &information)) {
    return false;
  }
  *identity = information.identity;
  return true;
}

NGLOG_ATTRIBUTE_NOINLINE
bool LogCleaner::RemoveLogIfUnchanged(const std::string& filepath,
                                      std::uintmax_t device,
                                      std::uintmax_t inode) {
#ifdef NGLOG_OS_WINDOWS
  // Verify and delete through the same handle, so no path-based step
  // remains for a rename to race with. GetFileIdentity() closes its handle
  // too soon to reuse here.
  const HANDLE file =
      CreateFileA(filepath.c_str(), DELETE | FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    ReportLastError("Could not open overdue log " + filepath);
    return false;
  }
  std::unique_ptr<void, decltype(&CloseHandle)> release{file, &CloseHandle};

  BY_HANDLE_FILE_INFORMATION file_information;
  if (!GetFileInformationByHandle(file, &file_information)) {
    ReportLastError("Could not stat overdue log " + filepath);
    return false;
  }

  std::uintmax_t current_device;
  std::uintmax_t current_inode;
  ExtractIdentity(file_information, &current_device, &current_inode);
  if (current_device != device || current_inode != inode) {
    return false;
  }

  FILE_DISPOSITION_INFO disposition{};
  disposition.DeleteFile = TRUE;
  if (!SetFileInformationByHandle(file, FileDispositionInfo, &disposition,
                                  sizeof(disposition))) {
    // NOTE May fail if another handle without FILE_SHARE_DELETE is open.
    ReportLastError("Could not remove overdue log " + filepath);
    return false;
  }
  return true;
#else
  FileIdentity identity;
  if (!GetFileIdentity(filepath, &identity)) {
    // May just mean the file was already removed, but a persistent failure
    // (e.g. a permission change) deserves a diagnostic.
    perror(("Could not stat overdue log " + filepath).c_str());
    return false;
  }
  if (identity.device != device || identity.inode != inode) {
    return false;
  }
  if (unlink(filepath.c_str()) != 0) {
    perror(("Could not remove overdue log " + filepath).c_str());
    return false;
  }
  return true;
#endif
}

}  // namespace internal
}  // namespace nglog
