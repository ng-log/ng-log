// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause

#ifndef NGLOG_INTERNAL_LOG_CLEANER_H_
#define NGLOG_INTERNAL_LOG_CLEANER_H_

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "internal/lock_metrics.h"
#include "ng-log/logging.h"

namespace nglog {
namespace internal {

class NGLOG_NO_EXPORT LogCleaner {
 public:
  LogCleaner() = default;
  ~LogCleaner();

  // Setting overdue to 0 days will delete all logs.
  void Enable(const std::chrono::minutes& overdue);
  void Disable();

  // Records the naming pattern of a newly created log file and wakes up the
  // cleaner thread so that logs which became overdue are removed promptly.
  // Called by LogFileObject whenever it creates a log file.
  void AddLogFilePattern(bool base_filename_selected,
                         const std::string& base_filename,
                         const std::string& filename_extension);

 private:
  // Naming pattern of the log files created by one LogFileObject together
  // with the point in time at which the matching directories are due for
  // another scan.
  struct LogFilePattern {
    bool base_filename_selected{false};
    std::string filename_extension;
    // Scans happen at fixed intervals, so schedule them on the steady clock
    // to stay unaffected by wall-clock adjustments (NTP, daylight saving).
    std::chrono::steady_clock::time_point next_cleanup_time;
  };

  // Body of the cleaner thread: sleeps until the earliest cleanup deadline
  // (or until woken up by Enable(), Disable() or AddLogFilePattern()) and
  // removes overdue logs of every pattern whose deadline has passed. Returns
  // as soon as the cleaner is disabled.
  void Worker();

  // Stops and joins the cleaner thread.
  void Stop();

  static void CleanOverdueLogs(
      const std::chrono::system_clock::time_point& current_time,
      const std::chrono::minutes& overdue, bool base_filename_selected,
      const std::string& base_filename, const std::string& filename_extension);

  static std::vector<std::string> GetOverdueLogNames(
      std::string log_directory,
      const std::chrono::system_clock::time_point& current_time,
      const std::chrono::minutes& overdue, const std::string& base_filename,
      const std::string& filename_extension);

  static bool IsLogFromCurrentProject(const std::string& filepath,
                                      const std::string& base_filename,
                                      const std::string& filename_extension);

  static bool IsLogLastModifiedOver(
      const std::string& filepath,
      const std::chrono::system_clock::time_point& current_time,
      const std::chrono::minutes& overdue);

  static constexpr std::intmax_t kSecondsInWeek = 60 * 60 * 24 * 7;

  // All members are guarded by mutex_. Scans run on the cleaner thread
  // without any lock held so that logging threads registering new log files
  // are never blocked on directory I/O.
  CleanerMutex mutex_;
  CleanerConditionVariable cond_;
  std::thread thread_;
  bool enabled_{false};
  std::chrono::minutes overdue_{
      std::chrono::duration<int, std::ratio<kSecondsInWeek>>{1}};
  // Maintain a separate cleanup deadline for each base_filename.
  std::unordered_map<std::string, LogFilePattern> patterns_;
};

extern NGLOG_NO_EXPORT LogCleaner g_log_cleaner;

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_LOG_CLEANER_H_
