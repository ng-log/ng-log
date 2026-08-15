// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_SOURCE_LOCATION_H
#define NGLOG_INTERNAL_SOURCE_LOCATION_H

#include <cstddef>
#include <string>

#include "ng-log/export.h"

namespace nglog {
namespace internal {

NGLOG_NO_EXPORT bool BuildFileUri(const char* path, std::size_t path_length,
                                  const char* base_path, const char* host,
                                  char* out, std::size_t out_size);

NGLOG_NO_EXPORT bool BuildFileLineUri(const char* span, std::size_t span_length,
                                      const char* base_path, const char* host,
                                      char* out, std::size_t out_size);

NGLOG_NO_EXPORT const std::string& CachedHostname();
NGLOG_NO_EXPORT const std::string& CachedCwd();

NGLOG_NO_EXPORT bool SplitFileLineSpan(
    const char* span, std::size_t span_length, const char** path,
    std::size_t* path_length, const char** line, std::size_t* line_length);

NGLOG_NO_EXPORT void FormatDisplayPath(const char* path,
                                       std::size_t path_length, const char* cwd,
                                       std::size_t prefix_components,
                                       std::size_t suffix_components, char* out,
                                       std::size_t out_size);

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_SOURCE_LOCATION_H
