// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Implements the hooks of the built-in attributes of the experimental logging
// functions.

#include "ng-log/experimental/attributes.h"

#include <atomic>
#include <chrono>
#include <compare>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

#include "context.h"
#include "internal/log_message_access.h"
#include "ng-log/raw_logging.h"

namespace nglog::experimental {

using internal::Destination;
using internal::Impl;
using internal::MessageImpl;

void internal::SeverityIsOutOfRange(LogSeverity severity) {
  using Value = std::underlying_type_t<LogSeverity>;
  RAW_LOG(FATAL, "Check failed: severity %d is not within [%d, %d]",
          static_cast<Value>(severity), static_cast<Value>(NGLOG_INFO),
          static_cast<Value>(NGLOG_FATAL));
  // A fatal raw message terminates unless a failure function returns.
  std::abort();
}

void Severity::Configure(Context& context) const {
  context.set_severity(value);
}

void FatalEscalation::Configure(Context& context) const {
  Impl(context).escalate_in_debug = true;
}

void Tag::Configure(Context& context) const { context.set_tag(name); }

void ErrnoDescription::Configure(Context& context) const {
  if (captured) {
    context.set_preserved_errno(value);
  }
}

void ErrnoDescription::Suffix(Message& message) const {
  nglog::internal::AppendErrnoDescription(
      static_cast<MessageImpl&>(message).message());
}

bool DebugOnly::Admit(Context& context) const { return context.debug(); }

// Verbose combines with SignalSafe, so signal handlers read the verbosity cache
// of a site, which only lock-free atomic operations allow.
static_assert(decltype(SiteFlag::level)::is_always_lock_free,
              "Signal-safe verbose logging reads the verbosity level of a "
              "call site through an atomic pointer, which is not lock-free "
              "on this platform. Disable the experimental logging functions "
              "with WITH_EXPERIMENTAL=OFF.");
static_assert(std::atomic<std::int32_t>::is_always_lock_free,
              "Signal-safe verbose logging reads atomic verbosity levels, "
              "which are not lock-free on this platform. Disable the "
              "experimental logging functions with WITH_EXPERIMENTAL=OFF.");
static_assert(decltype(SiteFlag::initialized)::is_always_lock_free,
              "Signal-safe verbose logging reads whether the verbosity of a "
              "call site is cached through an atomic flag, which is not "
              "lock-free on this platform. Disable the experimental logging "
              "functions with WITH_EXPERIMENTAL=OFF.");

template <internal::StandardInteger T>
bool Verbose<T>::Admit(Context& context) const {
  // Saturates levels outside the range of the verbosity flag instead of
  // wrapping around like a conversion would.
  constexpr std::int32_t kMinLevel = std::numeric_limits<std::int32_t>::min();
  constexpr std::int32_t kMaxLevel = std::numeric_limits<std::int32_t>::max();
  std::int32_t verbosity = kMaxLevel;
  if (std::cmp_less(level, kMinLevel)) {
    verbosity = kMinLevel;
  } else if (std::cmp_less_equal(level, kMaxLevel)) {
    verbosity = static_cast<std::int32_t>(level);
  }
  return nglog::internal::IsVLogOn(context.site().state<SiteFlag>(), verbosity,
                                   context.location().file_name());
}

template struct Verbose<signed char>;
template struct Verbose<unsigned char>;
template struct Verbose<short>;
template struct Verbose<unsigned short>;
template struct Verbose<int>;
template struct Verbose<unsigned>;
template struct Verbose<long>;
template struct Verbose<unsigned long>;
template struct Verbose<long long>;
template struct Verbose<unsigned long long>;

namespace {

template <class T>
struct CountState {
  // Wraps around at the limit of T without undefined behavior since atomic
  // arithmetic is defined to.
  std::atomic<T> occurrences{0};
  // Position within the current period of n occurrences, 0 before the first.
  std::atomic<T> occurrences_mod_n{0};
};

template <class T>
struct FirstState {
  std::atomic<T> occurrences{0};
};

struct PeriodState {
  std::atomic<std::int64_t> previous{0};
};

}  // namespace

template <internal::StandardInteger T>
bool internal::EveryCount<T>::Throttle(Context& context) const {
  auto& state = context.site().state<CountState<T>>();
  // Every occurrence counts like LOG_OCCURRENCES of LOG_EVERY_N(). The
  // counters publish no other data, so relaxed ordering suffices. Incrementing
  // the unsigned counterpart wraps around at the limit of T like the atomic
  // increment.
  using Unsigned = std::make_unsigned_t<T>;
  Impl(context).set_occurrences(
      static_cast<T>(static_cast<Unsigned>(state.occurrences.fetch_add(
                         T{1}, std::memory_order_relaxed)) +
                     Unsigned{1}));
  std::atomic<T>& position = state.occurrences_mod_n;
  // LOG_EVERY_N() never logs for a negative n and only logs the first
  // occurrence for zero.
  if constexpr (std::is_signed_v<T>) {
    if (n < 0) {
      return false;
    }
  }
  if (n == 0) {
    T expected{};
    return position.compare_exchange_strong(expected, T{1},
                                            std::memory_order_relaxed);
  }
  // Logs the first and every n-th occurrence like LOG_EVERY_N(). Wrapping to 1
  // instead of incrementing past n cannot overflow at the limit of T.
  T expected = position.load(std::memory_order_relaxed);
  T next;
  do {
    next = expected;
    if (next < n) {
      ++next;
    } else {
      next = T{1};
    }
  } while (!position.compare_exchange_weak(expected, next,
                                           std::memory_order_relaxed));
  return next == T{1};
}

template <internal::StandardInteger T>
bool internal::FirstCount<T>::Throttle(Context& context) const {
  std::atomic<T>& occurrences =
      context.site().state<FirstState<T>>().occurrences;
  // Logs the first n occurrences like LOG_FIRST_N(). Counting stops at n
  // instead of n + 1, which cannot overflow at the limit of T.
  T current = occurrences.load(std::memory_order_relaxed);
  while (current < n) {
    T next = current;
    ++next;
    if (occurrences.compare_exchange_weak(current, next,
                                          std::memory_order_relaxed)) {
      Impl(context).set_occurrences(next);
      return true;
    }
  }
  return false;
}

// Instantiates the rate limits for every counter type the header accepts.
template struct internal::EveryCount<signed char>;
template struct internal::EveryCount<unsigned char>;
template struct internal::EveryCount<short>;
template struct internal::EveryCount<unsigned short>;
template struct internal::EveryCount<int>;
template struct internal::EveryCount<unsigned>;
template struct internal::EveryCount<long>;
template struct internal::EveryCount<unsigned long>;
template struct internal::EveryCount<long long>;
template struct internal::EveryCount<unsigned long long>;

template struct internal::FirstCount<signed char>;
template struct internal::FirstCount<unsigned char>;
template struct internal::FirstCount<short>;
template struct internal::FirstCount<unsigned short>;
template struct internal::FirstCount<int>;
template struct internal::FirstCount<unsigned>;
template struct internal::FirstCount<long>;
template struct internal::FirstCount<unsigned long>;
template struct internal::FirstCount<long long>;
template struct internal::FirstCount<unsigned long long>;

bool internal::ThrottlePeriod(Context& context,
                              std::chrono::nanoseconds period) {
  auto& state = context.site().state<PeriodState>();
  // Mirrors the timing of LOG_EVERY_T().
  const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch());
  auto previous = state.previous.load(std::memory_order_relaxed);
  if (now - std::chrono::nanoseconds{previous} <= period) {
    return false;
  }
  // Only the thread that advances the time of the last message logs, so that
  // threads arriving together log once per period.
  return state.previous.compare_exchange_strong(previous, now.count(),
                                                std::memory_order_relaxed);
}

void SyslogDestination::Configure(Context& context) const {
  Impl(context).destination = Destination::kSyslog;
}

void ToSink::Configure(Context& context) const {
  Impl(context).destination = Destination::kSinkAndLog;
  Impl(context).AddSink(sink);
  Impl(context).AddStructuredSink(structured);
}

void LogFileExclusion::Configure(Context& context) const {
  Impl(context).skip_log_file = true;
}

void ToString::Configure(Context& context) const {
  Impl(context).destination = Destination::kStringAndLog;
  Impl(context).AddString(text);
}

void ToVector::Configure(Context& context) const {
  Impl(context).destination = Destination::kVectorOrLog;
  Impl(context).AddVector(texts);
}

void AsyncDelivery::Configure(Context& context) const {
  Impl(context).async = true;
}

}  // namespace nglog::experimental
