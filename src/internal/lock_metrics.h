// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_LOCK_METRICS_H_
#define NGLOG_INTERNAL_LOCK_METRICS_H_

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <shared_mutex>

#include "ng-log/export.h"

namespace nglog {
namespace internal {

enum class LockKind {
  kLog,
  kFile,
  kCleaner,
  kFatal,
  kSink,
  kCount,
};

struct LockMetrics {
  std::uint64_t acquisitions;
  std::uint64_t contended_acquisitions;
  std::uint64_t wait_nanoseconds;
  std::uint64_t hold_nanoseconds;
};

NGLOG_EXPORT void ResetLockMetrics();
NGLOG_EXPORT LockMetrics GetLockMetrics(LockKind kind);

template <LockKind Kind>
class InstrumentedMutex {
 public:
  void lock();
  void unlock();
  bool try_lock();

  InstrumentedMutex() = default;
  InstrumentedMutex(const InstrumentedMutex&) = delete;
  InstrumentedMutex& operator=(const InstrumentedMutex&) = delete;

 private:
  std::mutex mutex_;
};

template <LockKind Kind>
class InstrumentedRecursiveMutex {
 public:
  void lock();
  void unlock();
  bool try_lock();

  InstrumentedRecursiveMutex() = default;
  InstrumentedRecursiveMutex(const InstrumentedRecursiveMutex&) = delete;
  InstrumentedRecursiveMutex& operator=(const InstrumentedRecursiveMutex&) =
      delete;

 private:
  std::recursive_mutex mutex_;
};

template <LockKind Kind>
class InstrumentedSharedMutex {
 public:
  void lock();
  void unlock();
  bool try_lock();
  void lock_shared();
  void unlock_shared();
  bool try_lock_shared();

  InstrumentedSharedMutex() = default;
  InstrumentedSharedMutex(const InstrumentedSharedMutex&) = delete;
  InstrumentedSharedMutex& operator=(const InstrumentedSharedMutex&) = delete;

 private:
  std::shared_timed_mutex mutex_;
};

#ifdef NGLOG_ENABLE_LOCK_METRICS
using LogMutex = InstrumentedRecursiveMutex<LockKind::kLog>;
using FileMutex = InstrumentedMutex<LockKind::kFile>;
using CleanerMutex = InstrumentedMutex<LockKind::kCleaner>;
using FatalMutex = InstrumentedMutex<LockKind::kFatal>;
template <LockKind Kind>
using MetricsSharedMutex = InstrumentedSharedMutex<Kind>;
using CleanerConditionVariable = std::condition_variable_any;
#else
using LogMutex = std::recursive_mutex;
using FileMutex = std::mutex;
using CleanerMutex = std::mutex;
using FatalMutex = std::mutex;
using CleanerConditionVariable = std::condition_variable;
#endif

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_LOCK_METRICS_H_
