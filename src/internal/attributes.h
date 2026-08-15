// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_ATTRIBUTES_H
#define NGLOG_INTERNAL_ATTRIBUTES_H

#include <cassert>
#include <cstddef>
#include <utility>

#include "config.h"
#include "ng-log/platform.h"

// Applies format checking to a function that returns a format string.
#if defined(__GNUG__)
#  define NGLOG_ATTRIBUTE_FORMAT_ARG(stringIndex) \
    [[gnu::format_arg(stringIndex)]]
#else
#  define NGLOG_ATTRIBUTE_FORMAT_ARG(stringIndex)
#endif

// Requests inlining for functions on compilers that support the attribute.
#if defined(_MSC_VER)
#  define NGLOG_ATTRIBUTE_ALWAYS_INLINE [[msvc::forceinline]]
#elif defined(__has_attribute)
#  if __has_attribute(always_inline)
#    define NGLOG_ATTRIBUTE_ALWAYS_INLINE [[gnu::always_inline]]
#  endif
#endif

#if !defined(NGLOG_ATTRIBUTE_ALWAYS_INLINE)
#  define NGLOG_ATTRIBUTE_ALWAYS_INLINE
#endif

// Reserves a declaration hook for thread safety annotations.
#define NGLOG_LOCKS_REQUIRED(mu)

// Terminates execution after asserting in debug builds when unreachable code
// is reached.
#if defined(__cpp_lib_unreachable) && (__cpp_lib_unreachable >= 202202L)
#  define NGLOG_UNREACHABLE std::unreachable()
#elif !defined(NDEBUG)
#  define NGLOG_UNREACHABLE assert(false)
#else
#  if defined(_MSC_VER)
#    define NGLOG_UNREACHABLE __assume(false)
#  elif defined(__has_builtin)
#    if __has_builtin(unreachable)
#      define NGLOG_UNREACHABLE __builtin_unreachable()
#    endif
#  endif
#  if !defined(NGLOG_UNREACHABLE) && defined(__GNUG__)
#    define NGLOG_UNREACHABLE __builtin_unreachable()
#  endif
#  if !defined(NGLOG_UNREACHABLE)
#    define NGLOG_UNREACHABLE
#  endif
#endif

#endif  // NGLOG_INTERNAL_ATTRIBUTES_H
