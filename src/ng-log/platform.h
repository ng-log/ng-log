// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Shinichiro Hamaji
//
// Detect supported platforms.

#ifndef NGLOG_PLATFORM_H
#define NGLOG_PLATFORM_H

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
#  define NGLOG_OS_WINDOWS
#elif defined(__CYGWIN__) || defined(__CYGWIN32__)
#  define NGLOG_OS_CYGWIN
#elif defined(linux) || defined(__linux) || defined(__linux__)
#  define NGLOG_OS_LINUX
#  if defined(__ANDROID__)
#    define NGLOG_OS_ANDROID
#  endif
#elif defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__)
#  define NGLOG_OS_MACOSX
#elif defined(__FreeBSD__)
#  define NGLOG_OS_FREEBSD
#elif defined(__NetBSD__)
#  define NGLOG_OS_NETBSD
#elif defined(__OpenBSD__)
#  define NGLOG_OS_OPENBSD
#elif defined(__EMSCRIPTEN__)
#  define NGLOG_OS_EMSCRIPTEN
#else
// TODO(hamaji): Add other platforms.
#error Platform not supported by ng-log. Please consider to contribute platform information by submitting a pull request on Github.
#endif

#if defined(_MSC_VER)
#  define NGLOG_ATTRIBUTE_NOINLINE [[msvc::noinline]]
#elif defined(__has_attribute)
#  if __has_attribute(noinline)
#    define NGLOG_ATTRIBUTE_NOINLINE [[gnu::noinline]]
#  endif
#  if __has_attribute(used)
#    define NGLOG_ATTRIBUTE_USED [[gnu::used]]
#  endif
#endif

#if !defined(NGLOG_ATTRIBUTE_NOINLINE)
#  define NGLOG_ATTRIBUTE_NOINLINE
#endif

#if !defined(NGLOG_ATTRIBUTE_USED)
#  define NGLOG_ATTRIBUTE_USED
#endif

#if defined(__GNUG__)
#  define NGLOG_ATTRIBUTE_FORMAT(archetype, stringIndex, firstToCheck) \
    [[gnu::format(archetype, stringIndex, firstToCheck)]]
#else
#  define NGLOG_ATTRIBUTE_FORMAT(archetype, stringIndex, firstToCheck)
#endif

#if defined(__cpp_inline_variables) && (__cpp_inline_variables >= 201606L)
#  define NGLOG_INLINE_VARIABLE inline
#else
#  define NGLOG_INLINE_VARIABLE
#endif

#if defined(_MSC_VER)
#  define NGLOG_MSVC_PUSH_DISABLE_WARNING(n) \
    __pragma(warning(push)) __pragma(warning(disable : n))
#  define NGLOG_MSVC_POP_WARNING() __pragma(warning(pop))
#else
#  define NGLOG_MSVC_PUSH_DISABLE_WARNING(n)
#  define NGLOG_MSVC_POP_WARNING()
#endif

#endif  // NGLOG_PLATFORM_H
