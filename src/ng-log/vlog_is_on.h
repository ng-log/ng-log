// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Ray Sidney and many others
//
// Defines the VLOG_IS_ON macro that controls the variable-verbosity
// conditional logging.
//
// It's used by VLOG and VLOG_IF in logging.h
// and by RAW_VLOG in raw_logging.h to trigger the logging.
//
// It can also be used directly e.g. like this:
//   if (VLOG_IS_ON(2)) {
//     // do some logging preparation and logging
//     // that can't be accomplished e.g. via just VLOG(2) << ...;
//   }
//
// The truth value that VLOG_IS_ON(level) returns is determined by
// the three verbosity level flags:
//   --v=<n>  Gives the default maximal active V-logging level;
//            0 is the default.
//            Normally positive values are used for V-logging levels.
//   --vmodule=<str>  Gives the per-module maximal V-logging levels to override
//                    the value given by --v.
//                    E.g. "my_module=2,foo*=3" would change the logging level
//                    for source files whose bases are "my_module" and "foo*"
//                    ("-inl" suffixes are also disregarded for this matching).
//
// SetVLOGLevel helper function is provided to do limited dynamic control over
// V-logging by overriding the per-module settings given via --vmodule flag.
//
// CAVEAT: --vmodule functionality requires GNU C++ extensions.
//

#ifndef NGLOG_VLOG_IS_ON_H
#define NGLOG_VLOG_IS_ON_H

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "ng-log/export.h"
#include "ng-log/flags.h"
#include "ng-log/types.h"

namespace nglog {

// Set VLOG(_IS_ON) level for module_pattern to log_level.
// This lets us dynamically control what is normally set by the --vmodule flag.
// Returns the level that previously applied to module_pattern.
// NOTE: VLOG(_IS_ON) sites that have already executed and matched a pattern
// keep following that pattern, so changing their level requires the exact
// pattern that applied to them. Sites that matched no pattern follow the new
// level if module_pattern matches their module.
extern NGLOG_EXPORT int SetVLOGLevel(const char* module_pattern, int log_level);

// Declarations needed by VLOG_IS_ON below.

struct SiteFlag {
  // The pointer selects either the default or the module-specific level.
  std::atomic<std::atomic<int32>*> level;
  const nglog::Flag<std::int32_t>* default_level;
  std::atomic<bool> initialized;
  const char* base_name;
  std::size_t base_len;
  SiteFlag* next;
};

// Helper routine which determines the logging info for a particular VLOG site.
//   site_flag     is the site-local verbosity cache
//   site_default  is the synchronized default verbosity flag
//   fname         is the current source file name
//   verbose_level is the argument to VLOG_IS_ON
// We will return the return value for VLOG_IS_ON
// and if possible set the site cache appropriately.
extern NGLOG_EXPORT bool InitializeVLOG3(SiteFlag& site_flag,
                                         const Flag<std::int32_t>& site_default,
                                         const char* fname,
                                         int32 verbose_level);

namespace internal {

/**
 * Determines whether a VLOG site of the given verbosity logs. The site flag
 * caches whether --v or a --vmodule pattern controls the site.
 */
inline bool IsVLogOn(SiteFlag& site_flag, int32 verbose_level,
                     const char* fname) {
  std::atomic<int32>* const level =
      site_flag.level.load(std::memory_order_acquire);
  if (level != nullptr) {
    return level->load(std::memory_order_relaxed) >= verbose_level;
  }
  if (site_flag.initialized.load(std::memory_order_acquire)) {
    return GetFlag(*site_flag.default_level) >= verbose_level;
  }
  return InitializeVLOG3(site_flag, FLAGS_v, fname, verbose_level);
}

}  // namespace internal
}  // namespace nglog

#if defined(__GNUC__)
// We emit an anonymous static cache at every VLOG_IS_ON(n) site.
// (Normally) the first time every VLOG_IS_ON(n) site is hit,
// we determine what variable will dynamically control logging at this site:
// it's either FLAGS_v or an appropriate internal variable
// matching the current source file that represents results of
// parsing of --vmodule flag and/or SetVLOGLevel calls.
#  define VLOG_IS_ON(verboselevel)                                           \
    __extension__({                                                          \
      static nglog::SiteFlag nglog_vlog_site_;                               \
      nglog::internal::IsVLogOn(nglog_vlog_site_, (verboselevel), __FILE__); \
    })
#else
// GNU extensions not available, so we do not support --vmodule.
// Dynamic value of FLAGS_v always controls the logging level.
#  define VLOG_IS_ON(verboselevel) (nglog::GetFlag(FLAGS_v) >= (verboselevel))
#endif

#endif  // NGLOG_VLOG_IS_ON_H
