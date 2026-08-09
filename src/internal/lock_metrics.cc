// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "internal/lock_metrics.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <type_traits>

namespace nglog {
namespace internal {
namespace {

constexpr std::size_t kLockKindCount = static_cast<std::size_t>(
    static_cast<std::underlying_type_t<LockKind>>(LockKind::kCount));
using LockKindIndex = std::underlying_type_t<LockKind>;

struct MetricsStorage {
  std::array<std::atomic<std::uint64_t>, kLockKindCount> acquisitions{};
  std::array<std::atomic<std::uint64_t>, kLockKindCount>
      contended_acquisitions{};
  std::array<std::atomic<std::uint64_t>, kLockKindCount> wait_nanoseconds{};
  std::array<std::atomic<std::uint64_t>, kLockKindCount> hold_nanoseconds{};
};

#ifdef NGLOG_ENABLE_LOCK_METRICS
MetricsStorage metrics;
#endif

struct ThreadLockState {
  std::array<std::chrono::steady_clock::time_point, kLockKindCount>
      acquired_at{};
  std::array<bool, kLockKindCount> owns{};
};

thread_local ThreadLockState thread_lock_state;

void RecordAcquisition(LockKind kind,
                       std::chrono::steady_clock::duration wait) {
#ifdef NGLOG_ENABLE_LOCK_METRICS
  const LockKindIndex index = static_cast<LockKindIndex>(kind);
  metrics.acquisitions[index].fetch_add(1, std::memory_order_relaxed);
  if (wait > std::chrono::steady_clock::duration::zero()) {
    metrics.contended_acquisitions[index].fetch_add(1,
                                                    std::memory_order_relaxed);
  }
  metrics.wait_nanoseconds[index].fetch_add(
      static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(wait).count()),
      std::memory_order_relaxed);
#else
  (void)kind;
  (void)wait;
#endif
}

}  // namespace

void ResetLockMetrics() {
#ifdef NGLOG_ENABLE_LOCK_METRICS
  for (std::size_t i = 0; i < kLockKindCount; ++i) {
    metrics.acquisitions[i].store(0, std::memory_order_relaxed);
    metrics.contended_acquisitions[i].store(0, std::memory_order_relaxed);
    metrics.wait_nanoseconds[i].store(0, std::memory_order_relaxed);
    metrics.hold_nanoseconds[i].store(0, std::memory_order_relaxed);
  }
#endif
}

LockMetrics GetLockMetrics(LockKind kind) {
#ifdef NGLOG_ENABLE_LOCK_METRICS
  const LockKindIndex index = static_cast<LockKindIndex>(kind);
  return {
      metrics.acquisitions[index].load(std::memory_order_relaxed),
      metrics.contended_acquisitions[index].load(std::memory_order_relaxed),
      metrics.wait_nanoseconds[index].load(std::memory_order_relaxed),
      metrics.hold_nanoseconds[index].load(std::memory_order_relaxed),
  };
#else
  (void)kind;
  return {0, 0, 0, 0};
#endif
}

template <LockKind Kind>
void InstrumentedMutex<Kind>::lock() {
  const auto start = std::chrono::steady_clock::now();
  mutex_.lock();
  const auto acquired = std::chrono::steady_clock::now();
  const LockKindIndex index = static_cast<LockKindIndex>(Kind);
  RecordAcquisition(Kind, acquired - start);
  thread_lock_state.acquired_at[index] = acquired;
  thread_lock_state.owns[index] = true;
}

template <LockKind Kind>
void InstrumentedMutex<Kind>::unlock() {
  const LockKindIndex index = static_cast<LockKindIndex>(Kind);
#ifdef NGLOG_ENABLE_LOCK_METRICS
  if (thread_lock_state.owns[index]) {
    const auto hold =
        std::chrono::steady_clock::now() - thread_lock_state.acquired_at[index];
    metrics.hold_nanoseconds[index].fetch_add(
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(hold).count()),
        std::memory_order_relaxed);
  }
#endif
  thread_lock_state.owns[index] = false;
  mutex_.unlock();
}

template <LockKind Kind>
bool InstrumentedMutex<Kind>::try_lock() {
  if (!mutex_.try_lock()) {
    return false;
  }
  const auto acquired = std::chrono::steady_clock::now();
  RecordAcquisition(Kind, std::chrono::steady_clock::duration::zero());
  const LockKindIndex index = static_cast<LockKindIndex>(Kind);
  thread_lock_state.acquired_at[index] = acquired;
  thread_lock_state.owns[index] = true;
  return true;
}

template <LockKind Kind>
void InstrumentedSharedMutex<Kind>::lock() {
  const auto start = std::chrono::steady_clock::now();
  mutex_.lock();
  const auto acquired = std::chrono::steady_clock::now();
  const LockKindIndex index = static_cast<LockKindIndex>(Kind);
  RecordAcquisition(Kind, acquired - start);
  thread_lock_state.acquired_at[index] = acquired;
  thread_lock_state.owns[index] = true;
}

template <LockKind Kind>
void InstrumentedSharedMutex<Kind>::unlock() {
  const LockKindIndex index = static_cast<LockKindIndex>(Kind);
#ifdef NGLOG_ENABLE_LOCK_METRICS
  if (thread_lock_state.owns[index]) {
    const auto hold =
        std::chrono::steady_clock::now() - thread_lock_state.acquired_at[index];
    metrics.hold_nanoseconds[index].fetch_add(
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(hold).count()),
        std::memory_order_relaxed);
  }
#endif
  thread_lock_state.owns[index] = false;
  mutex_.unlock();
}

template <LockKind Kind>
bool InstrumentedSharedMutex<Kind>::try_lock() {
  if (!mutex_.try_lock()) {
    return false;
  }
  const auto acquired = std::chrono::steady_clock::now();
  RecordAcquisition(Kind, std::chrono::steady_clock::duration::zero());
  const LockKindIndex index = static_cast<LockKindIndex>(Kind);
  thread_lock_state.acquired_at[index] = acquired;
  thread_lock_state.owns[index] = true;
  return true;
}

template <LockKind Kind>
void InstrumentedSharedMutex<Kind>::lock_shared() {
  const auto start = std::chrono::steady_clock::now();
  mutex_.lock_shared();
  const auto acquired = std::chrono::steady_clock::now();
  const LockKindIndex index = static_cast<LockKindIndex>(Kind);
  RecordAcquisition(Kind, acquired - start);
  thread_lock_state.acquired_at[index] = acquired;
  thread_lock_state.owns[index] = true;
}

template <LockKind Kind>
void InstrumentedSharedMutex<Kind>::unlock_shared() {
  const LockKindIndex index = static_cast<LockKindIndex>(Kind);
#ifdef NGLOG_ENABLE_LOCK_METRICS
  if (thread_lock_state.owns[index]) {
    const auto hold =
        std::chrono::steady_clock::now() - thread_lock_state.acquired_at[index];
    metrics.hold_nanoseconds[index].fetch_add(
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(hold).count()),
        std::memory_order_relaxed);
  }
#endif
  thread_lock_state.owns[index] = false;
  mutex_.unlock_shared();
}

template <LockKind Kind>
bool InstrumentedSharedMutex<Kind>::try_lock_shared() {
  if (!mutex_.try_lock_shared()) {
    return false;
  }
  const auto acquired = std::chrono::steady_clock::now();
  RecordAcquisition(Kind, std::chrono::steady_clock::duration::zero());
  const LockKindIndex index = static_cast<LockKindIndex>(Kind);
  thread_lock_state.acquired_at[index] = acquired;
  thread_lock_state.owns[index] = true;
  return true;
}

template class InstrumentedMutex<LockKind::kLog>;
template class InstrumentedMutex<LockKind::kFile>;
template class InstrumentedMutex<LockKind::kCleaner>;
template class InstrumentedMutex<LockKind::kFatal>;
template class InstrumentedSharedMutex<LockKind::kSink>;

}  // namespace internal
}  // namespace nglog
