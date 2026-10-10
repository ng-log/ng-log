// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Stream output operators for STL containers; to be used for logging *only*.
// Inclusion of this file lets you do:
//
// list<string> x;
// LOG(INFO) << "data: " << x;
// vector<int> v1, v2;
// CHECK_EQ(v1, v2);
//

#ifndef NGLOG_STL_LOGGING_H
#define NGLOG_STL_LOGGING_H

#include <deque>
#include <list>
#include <map>
#include <ostream>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Forward declare these two, and define them after all the container streams
// operators so that we can recurse from pair -> container -> container -> pair
// properly.
template <class First, class Second>
std::ostream& operator<<(std::ostream& out, const std::pair<First, Second>& p);

namespace nglog {

template <class Iter>
void PrintSequence(std::ostream& out, Iter begin, Iter end);

}
#define OUTPUT_TWO_ARG_CONTAINER(Sequence)                       \
  template <class T1, class T2>                                  \
  inline std::ostream& operator<<(std::ostream& out,             \
                                  const Sequence<T1, T2>& seq) { \
    nglog::PrintSequence(out, seq.begin(), seq.end());           \
    return out;                                                  \
  }

OUTPUT_TWO_ARG_CONTAINER(std::vector)
OUTPUT_TWO_ARG_CONTAINER(std::deque)
OUTPUT_TWO_ARG_CONTAINER(std::list)
#undef OUTPUT_TWO_ARG_CONTAINER

#define OUTPUT_THREE_ARG_CONTAINER(Sequence)                         \
  template <class T1, class T2, class T3>                            \
  inline std::ostream& operator<<(std::ostream& out,                 \
                                  const Sequence<T1, T2, T3>& seq) { \
    nglog::PrintSequence(out, seq.begin(), seq.end());               \
    return out;                                                      \
  }

OUTPUT_THREE_ARG_CONTAINER(std::set)
OUTPUT_THREE_ARG_CONTAINER(std::multiset)

#undef OUTPUT_THREE_ARG_CONTAINER

#define OUTPUT_FOUR_ARG_CONTAINER(Sequence)                              \
  template <class T1, class T2, class T3, class T4>                      \
  inline std::ostream& operator<<(std::ostream& out,                     \
                                  const Sequence<T1, T2, T3, T4>& seq) { \
    nglog::PrintSequence(out, seq.begin(), seq.end());                   \
    return out;                                                          \
  }

OUTPUT_FOUR_ARG_CONTAINER(std::map)
OUTPUT_FOUR_ARG_CONTAINER(std::multimap)
OUTPUT_FOUR_ARG_CONTAINER(std::unordered_set)
OUTPUT_FOUR_ARG_CONTAINER(std::unordered_multiset)
#undef OUTPUT_FOUR_ARG_CONTAINER

#define OUTPUT_FIVE_ARG_CONTAINER(Sequence)                                  \
  template <class T1, class T2, class T3, class T4, class T5>                \
  inline std::ostream& operator<<(std::ostream& out,                         \
                                  const Sequence<T1, T2, T3, T4, T5>& seq) { \
    nglog::PrintSequence(out, seq.begin(), seq.end());                       \
    return out;                                                              \
  }

OUTPUT_FIVE_ARG_CONTAINER(std::unordered_map)
OUTPUT_FIVE_ARG_CONTAINER(std::unordered_multimap)

#undef OUTPUT_FIVE_ARG_CONTAINER

template <class First, class Second>
inline std::ostream& operator<<(std::ostream& out,
                                const std::pair<First, Second>& p) {
  out << '(' << p.first << ", " << p.second << ')';
  return out;
}

namespace nglog {

template <class Iter>
inline void PrintSequence(std::ostream& out, Iter begin, Iter end) {
  // Output at most 100 elements -- appropriate if used for logging.
  for (int i = 0; begin != end && i < 100; ++i, ++begin) {
    if (i > 0) out << ' ';
    out << *begin;
  }
  if (begin != end) {
    out << " ...";
  }
}

}  // namespace nglog

// Note that this is technically undefined behavior! We are adding things into
// the std namespace for a reason though -- we are providing new operations on
// types which are themselves defined with this namespace. Without this, these
// operator overloads cannot be found via ADL. If these definitions are not
// found via ADL, they must be #included before they're used, which requires
// this header to be included before apparently independent other headers.
//
// For example, base/logging.h defines various template functions to implement
// CHECK_EQ(x, y) and stream x and y into the log in the event the check fails.
// It does so via the function template MakeCheckOpValueString:
//   template<class T>
//   void MakeCheckOpValueString(strstream* ss, const T& v) {
//     (*ss) << v;
//   }
// Because 'glog/logging.h' is included before 'glog/stl_logging.h',
// subsequent CHECK_EQ(v1, v2) for vector<...> typed variable v1 and v2 can only
// find these operator definitions via ADL.
//
// Even this solution has problems -- it may pull unintended operators into the
// namespace as well, allowing them to also be found via ADL, and creating code
// that only works with a particular order of includes. Long term, we need to
// move all of the *definitions* into namespace std, bet we need to ensure no
// one references them first. This lets us take that step. We cannot define them
// in both because that would create ambiguous overloads when both are found.
namespace std {
using ::operator<<;
}

#endif  // NGLOG_STL_LOGGING_H
