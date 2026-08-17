// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "internal/log_cleaner.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "config.h"
#include "utilities.h"

#ifdef NGLOG_OS_WINDOWS
#  include "windows/dirent.h"
#else
#  include <dirent.h>
#endif

#include <sys/stat.h>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

namespace nglog {
namespace internal {
namespace {

#ifdef NGLOG_OS_WINDOWS
constexpr char possible_dir_delim[] = "\\/";
constexpr char path_delim = '\\';
#else
constexpr char possible_dir_delim[] = "/";
constexpr char path_delim = '/';
#endif

std::string JoinPath(const std::string& directory,
                     const std::string& filename) {
  if (directory == ".") {
    return filename;
  }
  if (!directory.empty() &&
      directory.find_last_of(possible_dir_delim) == directory.size() - 1) {
    return directory + filename;
  }
  return directory + path_delim + filename;
}

}  // namespace

NGLOG_NO_EXPORT LogCleaner g_log_cleaner;

LogCleaner::~LogCleaner() { Stop(); }

void LogCleaner::Stop() {
  std::thread cleaner;
  {
    std::lock_guard<CleanerMutex> l{mutex_};
    enabled_ = false;
    cleaner = std::move(thread_);
  }
  cond_.notify_one();
  if (cleaner.joinable()) {
    cleaner.join();
  }
}

void LogCleaner::Enable(const std::chrono::minutes& overdue) {
  {
    std::lock_guard<CleanerMutex> l{mutex_};
    overdue_ = overdue;
    if (!std::exchange(enabled_, true)) {
      // Logs may have become overdue while the cleaner was disabled: make all
      // known patterns due for an immediate scan.
      for (auto& pattern : patterns_) {
        pattern.second.next_cleanup_time = {};
      }
      thread_ = std::thread{&LogCleaner::Worker, this};
    }
  }
  // Wake up the cleaner thread: the overdue window may have changed.
  cond_.notify_one();
}

void LogCleaner::Disable() {
  Stop();

  // Callers disable the cleaner right before exiting and expect cleaning to
  // have happened by the time Disable() returns, so run a final scan here
  // rather than racing the cleaner thread for it. Stop() has already joined
  // that thread, so this runs uncontended.
  std::unordered_map<std::string, LogFilePattern> patterns;
  std::chrono::minutes overdue;
  {
    std::lock_guard<CleanerMutex> l{mutex_};
    patterns = patterns_;
    overdue = overdue_;
  }

  const auto current_time = std::chrono::system_clock::now();
  for (const auto& pattern : patterns) {
    CleanOverdueLogs(current_time, overdue, pattern.first, pattern.second);
  }
}

void LogCleaner::AddLogFile(LogFilenameSource filename_source,
                            const std::string& base_filename,
                            const std::string& filename,
                            const std::regex& filename_regex) {
  // The only caller, LogFileObject::Write(), already returns early when
  // filename_source is kSelected and base_filename is empty (that
  // combination means "don't write"), so this combination can never reach
  // here.
  assert(filename_source == LogFilenameSource::kDefault ||
         !base_filename.empty());

  const std::size_t separator = filename.find_last_of(possible_dir_delim);
  const std::string source_directory =
      separator == std::string::npos ? "." : filename.substr(0, separator + 1);
  const std::string relative_filename = separator == std::string::npos
                                            ? filename
                                            : filename.substr(separator + 1);

  {
    std::lock_guard<internal::CleanerMutex> l{mutex_};
    LogFilePattern& pattern = patterns_[base_filename];
    pattern.filename_source = filename_source;
    pattern.filename_regex = filename_regex;
    pattern.filenames_by_directory[source_directory].insert(relative_filename);
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
                  std::chrono::duration<int32>{FLAGS_logcleansecs});
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
      CleanOverdueLogs(current_time, overdue, pattern.first, pattern.second);
    }
    // New patterns may have been registered or the cleaner may have been
    // stopped while the lock was released: loop back to re-check both.
  }
}

void LogCleaner::CleanOverdueLogs(
    const std::chrono::system_clock::time_point& current_time,
    const std::chrono::minutes& overdue, const std::string& base_filename,
    const LogFilePattern& pattern) {
  std::set<std::string> dirs;

  if (pattern.filename_source == LogFilenameSource::kDefault) {
    for (const std::string& directory : GetLoggingDirectories()) {
      dirs.insert(directory.empty() ? "." : directory);
    }
  } else {
    const std::size_t pos = base_filename.find_last_of(possible_dir_delim);
    if (pos != std::string::npos) {
      const std::string directory = base_filename.substr(0, pos + 1);
      dirs.insert(directory.empty() ? "." : directory);
    } else {
      dirs.insert(".");
    }
  }

  for (const auto& filenames : pattern.filenames_by_directory) {
    dirs.insert(filenames.first.empty() ? "." : filenames.first);
  }

  for (const std::string& dir : dirs) {
    std::vector<std::string> logs =
        GetOverdueLogNames(dir, current_time, overdue, pattern);
    for (const std::string& log : logs) {
      // NOTE May fail on Windows if the file is still open.
      int result = unlink(log.c_str());
      if (result != 0) {
        perror(("Could not remove overdue log " + log).c_str());
      }
    }
  }
}

std::vector<std::string> LogCleaner::GetOverdueLogNames(
    std::string log_directory,
    const std::chrono::system_clock::time_point& current_time,
    const std::chrono::minutes& overdue, const LogFilePattern& pattern) {
  std::unordered_set<std::string> overdue_log_names;

  const auto exact_filenames =
      pattern.filenames_by_directory.find(log_directory);
  if (exact_filenames != pattern.filenames_by_directory.end()) {
    for (const std::string& filename : exact_filenames->second) {
      const std::string filepath = JoinPath(log_directory, filename);
      if (IsLogLastModifiedOver(filepath, current_time, overdue)) {
        overdue_log_names.insert(filepath);
      }
    }
  }

  // Try to get all files within log_directory.
  struct DirectoryDeleter {
    void operator()(DIR* directory) const noexcept { closedir(directory); }
  };
  using Directory = std::unique_ptr<DIR, DirectoryDeleter>;
  Directory dir{opendir(log_directory.c_str()), DirectoryDeleter{}};
  struct dirent* ent;

  if (dir) {
    while ((ent = readdir(dir.get())) != nullptr) {
      if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
        continue;
      }

      const std::string filename = ent->d_name;
      if (std::regex_match(filename, pattern.filename_regex)) {
        const std::string filepath = JoinPath(log_directory, filename);
        if (IsLogLastModifiedOver(filepath, current_time, overdue)) {
          overdue_log_names.insert(filepath);
        }
      }
    }
  }

  return {overdue_log_names.begin(), overdue_log_names.end()};
}

bool LogCleaner::IsLogLastModifiedOver(
    const std::string& filepath,
    const std::chrono::system_clock::time_point& current_time,
    const std::chrono::minutes& overdue) {
  // Try to get the last modified time of this file.
  struct stat file_stat;

  if (stat(filepath.c_str(), &file_stat) == 0) {
    const auto last_modified_time =
        std::chrono::system_clock::from_time_t(file_stat.st_mtime);
    const auto diff = current_time - last_modified_time;
    return diff >= overdue;
  }

  // If failed to get file stat, do not return true.
  return false;
}

}  // namespace internal
}  // namespace nglog
