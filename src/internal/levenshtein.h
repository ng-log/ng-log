// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_LEVENSHTEIN_H
#define NGLOG_INTERNAL_LEVENSHTEIN_H

#include <cstddef>
#include <string>
#include <vector>

namespace nglog {
namespace internal {

// Returns the minimum number of single-character edits needed to transform
// one string into another.
std::size_t LevenshteinDistance(const std::string& first,
                                const std::string& second);

// Returns the closest candidate when it is close enough to be a plausible
// misspelling. Returns an empty string when no candidate meets that bound.
std::string FindClosestMatch(const std::string& value,
                             const std::vector<std::string>& candidates);

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_LEVENSHTEIN_H
