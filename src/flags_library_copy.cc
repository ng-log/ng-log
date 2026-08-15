// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// A loadable module that carries its own copy of ng-log and defines a flag
// whose name every copy shares.

#include <cstdint>

#include "ng-log/flags.h"

#ifdef _WIN32
#  define NGLOG_LIBRARY_COPY_EXPORT __declspec(dllexport)
#else
#  define NGLOG_LIBRARY_COPY_EXPORT __attribute__((visibility("default")))
#endif

NGLOG_DEFINE(std::int32_t, library_copy_option, NGLOG_LIBRARY_COPY_VALUE,
             "Option defined by every copy of the test library.");

extern "C" NGLOG_LIBRARY_COPY_EXPORT std::int32_t LibraryCopyValue() {
  return nglog::GetFlag(FLAGS_library_copy_option);
}
