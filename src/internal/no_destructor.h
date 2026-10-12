// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_NO_DESTRUCTOR_H
#define NGLOG_INTERNAL_NO_DESTRUCTOR_H

#include <new>
#include <utility>

namespace nglog {
namespace internal {

/**
 * Holds an object that is constructed in place and never destroyed.
 *
 * Declared as a function-local static, the object is constructed the first
 * time control passes through its declaration, which the language makes
 * thread-safe. Since the wrapper has a trivial destructor, the object stays
 * usable from the constructors and destructors of other objects with static
 * or thread storage duration, regardless of the order in which those run.
 */
template <class T>
class NoDestructor {
 public:
  template <class... Args>
  explicit NoDestructor(Args&&... args);

  NoDestructor(const NoDestructor&) = delete;
  NoDestructor& operator=(const NoDestructor&) = delete;

  T& operator*() const noexcept;
  T* operator->() const noexcept;
  T* get() const noexcept;

 private:
  alignas(T) unsigned char storage_[sizeof(T)];
  T* object_;
};

template <class T>
template <class... Args>
NoDestructor<T>::NoDestructor(Args&&... args)
    : object_{::new (static_cast<void*>(storage_))
                  T(std::forward<Args>(args)...)} {}

template <class T>
T& NoDestructor<T>::operator*() const noexcept {
  return *object_;
}

template <class T>
T* NoDestructor<T>::operator->() const noexcept {
  return object_;
}

template <class T>
T* NoDestructor<T>::get() const noexcept {
  return object_;
}

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_NO_DESTRUCTOR_H
