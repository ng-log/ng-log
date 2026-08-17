// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "internal/log_cleaner.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
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
#else
constexpr char possible_dir_delim[] = "/";
#endif

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
    const std::size_t pos = base_filename.find_last_of(possible_dir_delim);
    if (pos != std::string::npos) {
      dirs.emplace_back(base_filename, 0, pos + 1);
    } else {
      dirs.emplace_back(".");
    }
  }

  for (const std::string& dir : dirs) {
    std::vector<std::string> logs = GetOverdueLogNames(
        dir, current_time, overdue, base_filename, filename_extension);
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
    const std::chrono::minutes& overdue, const std::string& base_filename,
    const std::string& filename_extension) {
  // The names of overdue logs.
  std::vector<std::string> overdue_log_names;

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

      std::string filepath = ent->d_name;
      if (!log_directory.empty() &&
          log_directory.find_last_of(possible_dir_delim) ==
              log_directory.size() - 1) {
        filepath = log_directory + filepath;
      }

      if (IsLogFromCurrentProject(filepath, base_filename,
                                  filename_extension) &&
          IsLogLastModifiedOver(filepath, current_time, overdue)) {
        overdue_log_names.push_back(filepath);
      }
    }
  }

  return overdue_log_names;
}

bool LogCleaner::IsLogFromCurrentProject(
    const std::string& filepath, const std::string& base_filename,
    const std::string& filename_extension) {
  // We should remove duplicated delimiters from `base_filename`, e.g.,
  // before: "/tmp//<base_filename>.<create_time>.<pid>"
  // after:  "/tmp/<base_filename>.<create_time>.<pid>"
  std::string cleaned_base_filename =
      CollapseRepeatedCharacters(base_filename, possible_dir_delim);
  std::size_t real_filepath_size = filepath.size();

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
    if (IsFilenameExtensionAfterBaseFilename(filepath, cleaned_base_filename,
                                             filename_extension)) {
      cleaned_base_filename += filename_extension;
    } else {
      // For the new version, filename_extension is at the filepath end.
      if (!IsFilenameExtensionAtEnd(filepath, filename_extension)) {
        return false;
      }
      real_filepath_size -= filename_extension.size();
    }
  }

  // The characters after cleaned_base_filename should match
  // YYYYMMDD-HHMMSS.pid.
  for (std::size_t i = cleaned_base_filename.size(); i < real_filepath_size;
       ++i) {
    const char& c = filepath[i];

    if (i <= cleaned_base_filename.size() + 7) {
      if (c < '0' || c > '9') {
        return false;
      }
    } else if (i == cleaned_base_filename.size() + 8) {
      if (c != '-') {
        return false;
      }
    } else if (i <= cleaned_base_filename.size() + 14) {
      if (c < '0' || c > '9') {
        return false;
      }
    } else if (i == cleaned_base_filename.size() + 15) {
      if (c != '.') {
        return false;
      }
    } else if (i >= cleaned_base_filename.size() + 16) {
      if (c < '0' || c > '9') {
        return false;
      }
    }
  }

  return true;
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
