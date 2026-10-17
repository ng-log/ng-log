// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "levenshtein.h"

#include <algorithm>
#include <limits>

namespace nglog {
namespace internal {

std::size_t LevenshteinDistance(const std::string& first,
                                const std::string& second) {
  std::vector<std::size_t> previous(second.size() + 1);
  std::vector<std::size_t> current(second.size() + 1);
  for (std::size_t index = 0; index <= second.size(); ++index) {
    previous[index] = index;
  }

  for (std::size_t first_index = 0; first_index < first.size(); ++first_index) {
    current[0] = first_index + 1;
    for (std::size_t second_index = 0; second_index < second.size();
         ++second_index) {
      const std::size_t deletion = previous[second_index + 1] + 1;
      const std::size_t insertion = current[second_index] + 1;
      const std::size_t substitution =
          previous[second_index] +
          (first[first_index] == second[second_index] ? 0U : 1U);
      current[second_index + 1] = std::min({deletion, insertion, substitution});
    }
    previous.swap(current);
  }
  return previous.back();
}

std::string FindClosestMatch(const std::string& value,
                             const std::vector<std::string>& candidates) {
  constexpr std::size_t kMinimumSuggestionDistance = 2;
  std::string closest;
  std::size_t closest_distance = std::numeric_limits<std::size_t>::max();
  for (const std::string& candidate : candidates) {
    const std::size_t distance = LevenshteinDistance(value, candidate);
    // A distance of the length of the longer string allows replacing every
    // character, so such a candidate does not count as related to the input.
    const std::size_t replacement_distance =
        std::max(value.size(), candidate.size());
    if (distance < replacement_distance && distance < closest_distance) {
      closest_distance = distance;
      closest = candidate;
    }
  }

  const std::size_t maximum_distance =
      std::max(kMinimumSuggestionDistance, value.size() / 2);
  return closest_distance <= maximum_distance ? closest : std::string();
}

}  // namespace internal
}  // namespace nglog
