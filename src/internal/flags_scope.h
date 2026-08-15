// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_SRC_INTERNAL_FLAGS_SCOPE_H_
#define NGLOG_SRC_INTERNAL_FLAGS_SCOPE_H_

#include <tuple>
#include <type_traits>
#include <utility>

namespace nglog {
namespace internal {

template <typename Flag>
class FlagsScopePair {
 public:
  template <typename NewValue>
  FlagsScopePair(Flag& flag, NewValue&& new_value)
      : flag_(&flag),
        old_value_(std::exchange(flag, std::forward<NewValue>(new_value))) {}

  FlagsScopePair(const FlagsScopePair&) = delete;
  FlagsScopePair& operator=(const FlagsScopePair&) = delete;

  FlagsScopePair(FlagsScopePair&& other) noexcept(
      std::is_nothrow_move_constructible<Flag>::value)
      : flag_(std::exchange(other.flag_, nullptr)),
        old_value_(std::move(other.old_value_)) {}

  FlagsScopePair& operator=(FlagsScopePair&&) = delete;

  ~FlagsScopePair() { Restore(); }

 private:
  void Restore() {
    if (flag_ != nullptr) {
      *flag_ = std::move(old_value_);
      flag_ = nullptr;
    }
  }

  Flag* flag_;
  Flag old_value_;
};

template <typename Flag, typename Value>
FlagsScopePair<Flag> MakeFlagsScopePair(Flag& flag, Value&& new_value) {
  return FlagsScopePair<Flag>(flag, std::forward<Value>(new_value));
}

template <typename... Pairs>
class FlagsScope {
 public:
  explicit FlagsScope(Pairs&&... pairs) : pairs_(std::move(pairs)...) {}

  FlagsScope(const FlagsScope&) = delete;
  FlagsScope& operator=(const FlagsScope&) = delete;
  FlagsScope(FlagsScope&&) = default;
  FlagsScope& operator=(FlagsScope&&) = delete;
  ~FlagsScope() = default;

 private:
  std::tuple<Pairs...> pairs_;
};

template <typename... Pairs>
auto MakeFlagsScope(Pairs&&... pairs) {
  return FlagsScope<std::decay_t<Pairs>...>{std::forward<Pairs>(pairs)...};
}

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_SRC_INTERNAL_FLAGS_SCOPE_H_
