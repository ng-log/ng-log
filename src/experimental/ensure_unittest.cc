// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Verifies that Ensure(), the comparisons and NotNull() behave like the CHECK
// macros, also during constant evaluation.

#include "ng-log/experimental/ensure.h"

#include <gmock/gmock.h>
#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// Enables the regular destinations of Ensure().
#include "ng-log/experimental/log.h"  // IWYU pragma: keep
#include "ng-log/logging.h"
#include "testing.h"

namespace {

using namespace ngx_testing;
using testing::AllOf;
using testing::ElementsAre;
using testing::HasSubstr;
using testing::IsEmpty;
using testing::Not;

TEST_F(ExperimentalLogging, EnsureFailureMatchesMacro) {
  const int value = -1;
  EXPECT_SAME_DEATH(CHECK(value > 0) << "got " << value,
                    ngx::Ensure(value > 0, "value > 0 got {}", value));
}

TEST_F(ExperimentalLogging, EnsureWithWideMessageMatchesMacro) {
  const int value = -1;
  EXPECT_SAME_DEATH(CHECK(value > 0) << "got " << value,
                    ngx::Ensure(value > 0, L"value > 0 got {}", value));
}

TEST_F(ExperimentalLogging, EnsureWithErrnoAndWideMessageMatchesMacro) {
  const bool opened = false;
  EXPECT_SAME_DEATH(
      (errno = EACCES, PCHECK(opened) << "file"),
      (errno = EACCES, ngx::Ensure(opened, ngx::Errno, L"opened file")));
}

TEST_F(ExperimentalLogging, ComparisonWithWideMessageMatchesMacro) {
  const int a = 1;
  EXPECT_SAME_DEATH(CHECK_EQ(a, 2) << "file " << a,
                    ngx::Ensure(NGLOG_COMPARE(a == 2), L"file {}", a));
}

#ifdef GTEST_HAS_DEATH_TEST
TEST_F(ExperimentalLogging, ThatWithWideMessageDescribesFailure) {
  const int a = 1;
  EXPECT_DEATH(ngx::Ensure(ngx::That(a) == 2, L"file {}", a),
               "Check failed: lhs == rhs \\(1 vs\\. 2\\) file 1");
}
#endif

#ifdef GTEST_HAS_DEATH_TEST
// A failed check terminates although FatalInDebug marks it as a debug fatal
// message and DFATAL messages do not terminate.
TEST_F(ExperimentalLogging, EnsureWithFatalInDebugTerminates) {
  const ScopedExitOnDFatal exit_on_dfatal{false};
  EXPECT_DEATH(ngx::Ensure(false, ngx::FatalInDebug, "must terminate"),
               "must terminate");
}
#endif

TEST_F(ExperimentalLogging, EnsureSuccessIsSilent) {
  const Output output = Capture([] { ngx::Ensure(true, "never {}", 1); });
  EXPECT_THAT(output.stderr_text, IsEmpty());
  EXPECT_THAT(output.sink_messages, IsEmpty());
}

TEST_F(ExperimentalLogging, EnsureWithErrnoMatchesMacro) {
  const bool opened = false;
  EXPECT_SAME_DEATH(
      (errno = EACCES, PCHECK(opened) << "file"),
      (errno = EACCES, ngx::Ensure(opened, ngx::Errno, "opened file")));
}

#if DCHECK_IS_ON()
TEST_F(ExperimentalLogging, DebugEnsureMatchesMacro) {
  const int value = 0;
  EXPECT_SAME_DEATH(DCHECK(value == 1) << "debug",
                    ngx::Ensure(value == 1, ngx::Debug, "value == 1 debug"));
}
#else
TEST_F(ExperimentalLogging, DebugEnsureIsDisabled) {
  constexpr int kValue = 0;
  const Output output = Capture(
      [] { ngx::Ensure(kValue == 1, ngx::Debug, "kValue == 1 debug"); });
  EXPECT_THAT(output.stderr_text, IsEmpty());
}
#endif

TEST_F(ExperimentalLogging, EnsureNeMatchesMacro) {
  const int a = 1;
  EXPECT_SAME_DEATH(CHECK_NE(a, 1) << "msg",
                    ngx::Ensure(ngx::Ne(a, 1, "a != 1"), "msg"));
}

TEST_F(ExperimentalLogging, EnsureLtMatchesMacro) {
  const std::size_t i = 5;
  const std::size_t n = 3;
  EXPECT_SAME_DEATH(CHECK_LT(i, n), ngx::Ensure(ngx::Lt(i, n, "i < n"), ""));
}

TEST_F(ExperimentalLogging, EnsureGeMatchesMacro) {
  const std::string s = "abc";
  const std::string t = "abd";
  EXPECT_SAME_DEATH(CHECK_GE(s, t) << "order",
                    ngx::Ensure(ngx::Ge(s, t, "s >= t"), "order"));
}

// Unlike the CHECK_EQ() family, integers of mixed signedness compare by value
// instead of converting a negative operand to a large unsigned value.
TEST(ExperimentalEnsure, MixedSignednessComparesValues) {
  const int negative = -1;
  const unsigned positive = 3;
  const unsigned max = std::numeric_limits<unsigned>::max();
  EXPECT_THAT(ngx::Eq(negative, max, "negative == max"), Not(Holds()));
  EXPECT_THAT(ngx::Ne(negative, max, "negative != max"), Holds());
  EXPECT_THAT(ngx::Lt(negative, positive, "negative < positive"), Holds());
  EXPECT_THAT(ngx::Le(negative, positive, "negative <= positive"), Holds());
  EXPECT_THAT(ngx::Gt(negative, positive, "negative > positive"), Not(Holds()));
  EXPECT_THAT(ngx::Ge(negative, positive, "negative >= positive"),
              Not(Holds()));
}

TEST_F(ExperimentalLogging, CompareEqMatchesMacro) {
  const int a = 1;
  const int b = 2;
  EXPECT_SAME_DEATH(CHECK_EQ(a, b) << "msg " << 3,
                    ngx::Ensure(NGLOG_COMPARE(a == b), "msg {}", 3));
}

TEST_F(ExperimentalLogging, CompareNeMatchesMacro) {
  const int a = 1;
  EXPECT_SAME_DEATH(CHECK_NE(a, 1), ngx::Ensure(NGLOG_COMPARE(a != 1), ""));
}

TEST_F(ExperimentalLogging, CompareLtMatchesMacro) {
  const std::string s = "abd";
  const std::string t = "abc";
  EXPECT_SAME_DEATH(CHECK_LT(s, t), ngx::Ensure(NGLOG_COMPARE(s < t), ""));
}

TEST_F(ExperimentalLogging, CompareLeMatchesMacro) {
  const double x = 2.5;
  EXPECT_SAME_DEATH(CHECK_LE(x, 1.0), ngx::Ensure(NGLOG_COMPARE(x <= 1.0), ""));
}

TEST_F(ExperimentalLogging, CompareGtMatchesMacro) {
  const int a = 1;
  const int b = 2;
  EXPECT_SAME_DEATH(CHECK_GT(a, b), ngx::Ensure(NGLOG_COMPARE(a > b), ""));
}

TEST_F(ExperimentalLogging, CompareGeMatchesMacro) {
  const int a = 1;
  const int b = 2;
  EXPECT_SAME_DEATH(CHECK_GE(a, b), ngx::Ensure(NGLOG_COMPARE(a >= b), ""));
}

// Operators binding tighter than comparisons belong to the operands.
TEST_F(ExperimentalLogging, CompareKeepsArithmeticInOperands) {
  const int a = 1;
  const int b = 2;
  EXPECT_SAME_DEATH(CHECK_EQ(a + b, b * 2),
                    ngx::Ensure(NGLOG_COMPARE(a + b == b * 2), ""));
}

// The left operand evaluates its arithmetic like the built-in operators.
TEST(ExperimentalEnsure, CompareEvaluatesArithmeticOfLeftOperand) {
  const int a = 7;
  const int b = 3;
  const std::string s = "ab";
  EXPECT_THAT(NGLOG_COMPARE(a + b * 2 - 1 == 12), Holds());
  EXPECT_THAT(NGLOG_COMPARE(a / b % 2 == 0), Holds());
  EXPECT_THAT(NGLOG_COMPARE(a - b - 1 == 3), Holds());
  EXPECT_THAT(NGLOG_COMPARE(s + "c" == "abc"), Holds());
  EXPECT_THAT(NGLOG_COMPARE(a % b + 1 < 2), Not(Holds()));
}

#ifdef GTEST_HAS_DEATH_TEST
// Commas inside the expression, such as in template arguments, are kept.
TEST_F(ExperimentalLogging, CompareAcceptsCommas) {
  EXPECT_DEATH(ngx::Ensure(NGLOG_COMPARE(std::max<int>(1, 2) == 3), ""),
               "Check failed: std::max<int>\\(1, 2\\) == 3 \\(2 vs\\. 3\\)");
}

TEST_F(ExperimentalLogging, CompareAcceptsAttributes) {
  const int a = 1;
  EXPECT_DEATH(
      (errno = EACCES, ngx::Ensure(NGLOG_COMPARE(a == 2), ngx::Errno, "file")),
      "Check failed: a == 2 \\(1 vs\\. 2\\) file: .* \\[13\\]");
}
#endif

// That() compares like NGLOG_COMPARE() without a macro.
TEST(ExperimentalEnsure, ThatComparesOperands) {
  const int a = 7;
  const int b = 3;
  EXPECT_THAT(ngx::That(a) == 7, Holds());
  EXPECT_THAT(ngx::That(a) != 7, Not(Holds()));
  EXPECT_THAT(ngx::That(a) < b, Not(Holds()));
  EXPECT_THAT(ngx::That(a) <= 7, Holds());
  EXPECT_THAT(ngx::That(a) > b, Holds());
  EXPECT_THAT(ngx::That(a) >= 8, Not(Holds()));
}

// The parentheses of That() admit operators that NGLOG_COMPARE() rejects in
// its left operand.
TEST(ExperimentalEnsure, ThatAcceptsAnyLeftOperand) {
  const int a = 7;
  const int b = 3;
  EXPECT_THAT(ngx::That(a > 0 && b > 0) == true, Holds());
  EXPECT_THAT(ngx::That(a << 1 | 1) == 15, Holds());
  EXPECT_THAT(ngx::That(a > b ? a : b) == 7, Holds());
  EXPECT_THAT(ngx::That(a + b * 2) == 13, Holds());
}

// Characters compare by value like other integers. Only where char is signed
// does this differ from converting both operands to unsigned.
TEST(ExperimentalEnsure, ThatComparesCharactersByValue) {
  constexpr char kNegative = static_cast<char>(-1);
  if constexpr (std::is_signed_v<char>) {
    EXPECT_THAT(ngx::That(kNegative) < 1U, Holds());
    EXPECT_THAT(ngx::Lt(kNegative, 1U, "kNegative < 1U"), Holds());
    EXPECT_THAT(ngx::That(kNegative) == ~0U, Not(Holds()));
  } else {
    GTEST_SKIP() << "char is unsigned";
  }
}

// Without operand names, the description names the operator.
TEST(ExperimentalEnsure, ThatPrintsOperatorAndOperands) {
  const int a = 1;
  EXPECT_EQ(testing::PrintToString(ngx::That(a) == 2), "lhs == rhs (1 vs. 2)");
  EXPECT_EQ(testing::PrintToString(ngx::That(a) != 1), "lhs != rhs (1 vs. 1)");
  EXPECT_EQ(testing::PrintToString(ngx::That(a) < 0), "lhs < rhs (1 vs. 0)");
  EXPECT_EQ(testing::PrintToString(ngx::That(a) <= 0), "lhs <= rhs (1 vs. 0)");
  EXPECT_EQ(testing::PrintToString(ngx::That(a) > 2), "lhs > rhs (1 vs. 2)");
  EXPECT_EQ(testing::PrintToString(ngx::That(a) >= 2), "lhs >= rhs (1 vs. 2)");
}

// A nullable handle that supports neither stream insertion nor std::format.
struct UnprintableHandle {
  using element_type = int;

  int* pointer = nullptr;

  friend bool operator==(const UnprintableHandle& handle,
                         std::nullptr_t /*null*/) {
    return handle.pointer == nullptr;
  }
};

// Supports stream insertion but not std::format.
struct Streamed {
  int value;

  friend bool operator==(const Streamed& lhs, const Formattable& rhs) {
    return lhs.value == rhs.value;
  }

  friend std::ostream& operator<<(std::ostream& stream, const Streamed& value) {
    return stream << "streamed " << value.value;
  }
};

// Each operand is rendered on its own, so that a streamable operand can be
// compared with one that only supports std::format.
TEST(ExperimentalEnsure, ThatRendersEachOperandOnItsOwn) {
  EXPECT_EQ(testing::PrintToString(ngx::That(Streamed{1}) == Formattable{2}),
            "lhs == rhs (streamed 1 vs. 2)");
}

// NotNull() never prints the operand, so it accepts nullable types that can
// be neither streamed nor formatted.
TEST(ExperimentalEnsure, NotNullReturnsUnprintableHandle) {
  int value = 1;
  const UnprintableHandle handle{&value};
  EXPECT_EQ(ngx::Ensure(ngx::NotNull(handle, "handle")).pointer, &value);
}

// Counts the live instances, which its comparison reports.
struct Counted {
  static inline int live = 0;

  Counted() { ++live; }
  Counted(const Counted& /*other*/) { ++live; }
  Counted(Counted&& /*other*/) noexcept { ++live; }
  Counted& operator=(const Counted& other) = default;
  Counted& operator=(Counted&& other) noexcept = default;
  ~Counted() { --live; }

  friend bool operator==(const Counted& /*counted*/, int expected) {
    return live == expected;
  }

  friend std::ostream& operator<<(std::ostream& stream,
                                  const Counted& /*counted*/) {
    return stream << live << " live";
  }
};

// Counts its copies and moves.
struct CopyCounter {
  static inline int copies = 0;
  static inline int moves = 0;

  CopyCounter() = default;
  CopyCounter(const CopyCounter& /*other*/) { ++copies; }
  CopyCounter(CopyCounter&& /*other*/) noexcept { ++moves; }
  CopyCounter& operator=(const CopyCounter& other) = delete;
  CopyCounter& operator=(CopyCounter&& other) = delete;
  ~CopyCounter() = default;

  friend bool operator==(const CopyCounter& /*counter*/, int /*value*/) {
    return true;
  }

  friend std::ostream& operator<<(std::ostream& stream,
                                  const CopyCounter& /*counter*/) {
    return stream << "counter";
  }
};

// That() references an lvalue and moves an rvalue instead of copying it, once
// into the operand and once into the comparison.
TEST(ExperimentalEnsure, ThatForwardsOperands) {
  constexpr int kMoves = 2;
  const CopyCounter lvalue;
  CopyCounter::copies = 0;
  CopyCounter::moves = 0;
  EXPECT_THAT(ngx::That(lvalue) == 1, Holds());
  EXPECT_EQ(CopyCounter::copies, 0);
  EXPECT_EQ(CopyCounter::moves, 0);
  EXPECT_THAT(ngx::That(CopyCounter{}) == 1, Holds());
  EXPECT_EQ(CopyCounter::copies, 0);
  EXPECT_EQ(CopyCounter::moves, kMoves);
}

// That() takes ownership of an rvalue, so a stored operand does not refer to
// a destroyed temporary.
TEST(ExperimentalEnsure, ThatOwnsRvalueOperand) {
  auto operand = ngx::That(Counted{});
  EXPECT_THAT(std::move(operand) == 1, Holds());
}

// Unlike CHECK_EQ(), That() compares C strings by their contents.
TEST(ExperimentalEnsure, ThatComparesCStringContents) {
  const char* const s = "abc";
  const std::string copy = "abc";
  char buffer[] = "abc";
  const char* const null = nullptr;
  EXPECT_THAT(ngx::That(s) == copy.c_str(), Holds());
  EXPECT_THAT(ngx::That(s) != copy.c_str(), Not(Holds()));
  EXPECT_THAT(ngx::That(buffer) == "abc", Holds());
  EXPECT_THAT(ngx::That(s) == "abd", Not(Holds()));
  // Like CHECK_STREQ(), null equals only null.
  EXPECT_THAT(ngx::That(null) == null, Holds());
  EXPECT_THAT(ngx::That(null) == "", Not(Holds()));
}

// AsPointer() compares C strings by address instead of by contents.
TEST(ExperimentalEnsure, ThatAsPointerComparesAddresses) {
  const char* const s = "abc";
  const std::string copy = "abc";
  const wchar_t* const wide = L"abc";
  EXPECT_THAT(ngx::That(s).AsPointer() == copy.c_str(), Not(Holds()));
  EXPECT_THAT(ngx::That(s).AsPointer() != copy.c_str(), Holds());
  EXPECT_THAT(ngx::That(s).AsPointer() == s, Holds());
  EXPECT_THAT(ngx::That(wide).AsPointer() == wide, Holds());
}

#ifdef GTEST_HAS_DEATH_TEST
// The failure prints addresses, since the characters need not be terminated.
// The characters cannot occur in an address, whose format depends on the
// standard library, such as 0x7ffc1234 or 010FFAD8.
TEST_F(ExperimentalLogging, ThatAsPointerFailurePrintsAddresses) {
  const char first[] = {'x', 'y', 'z'};
  const char second[] = {'x', 'y', 'z'};
  EXPECT_DEATH(ngx::Ensure(ngx::That(first).AsPointer() == second),
               AllOf(HasSubstr("Check failed: lhs == rhs ("),
                     HasSubstr(" vs. "), Not(HasSubstr("xyz"))));
}

// The bounds of a range of addresses are printed as addresses as well. Both
// point to characters that are not terminated, which would be printed as text.
TEST_F(ExperimentalLogging, ThatInRangeFailurePrintsAddressBounds) {
  const char characters[] = {'x', 'y', 'z', 'u', 'v', 'w'};
  constexpr std::size_t kHalf = std::size(characters) / 2;
  const char* const low = characters;
  const char* const high = characters + kHalf;
  const char* const outside = high + 1;
  EXPECT_DEATH(ngx::Ensure(ngx::That(outside).AsPointer().InRange(low, high)),
               AllOf(HasSubstr("Check failed: lhs in range ("),
                     Not(HasSubstr("xyz")), Not(HasSubstr("uvw"))));
}

// Pointers to other character types may point to bytes that are not
// terminated, so they are printed as addresses.
TEST_F(ExperimentalLogging, ThatBytePointerFailurePrintsAddress) {
  const unsigned char bytes[] = {'x', 'y', 'z'};
  const unsigned char* const p = bytes;
  EXPECT_DEATH(
      ngx::Ensure(ngx::That(p) == nullptr),
      AllOf(HasSubstr("Check failed: lhs == rhs ("), Not(HasSubstr("xyz"))));
}
#endif

// Casting an operand to const void* compares the addresses as well.
TEST(ExperimentalEnsure, ThatComparesVoidPointersByAddress) {
  const char* const s = "abc";
  const std::string copy = "abc";
  EXPECT_THAT(ngx::That(static_cast<const void*>(s)) == copy.c_str(),
              Not(Holds()));
  EXPECT_THAT(ngx::That(static_cast<const void*>(s)) == s, Holds());
}

// Arrays other than C strings compare as pointers, since C++26 rejects
// comparing two arrays.
TEST(ExperimentalEnsure, ThatComparesArraysAsPointers) {
  const int first[] = {1, 2};
  const int second[] = {1, 2};
  EXPECT_THAT(ngx::That(first) == second, Not(Holds()));
  EXPECT_THAT(ngx::That(first) == first, Holds());
}

TEST(ExperimentalEnsure, ThatIgnoreCaseComparesCStrings) {
  const char* const s = "abc";
  EXPECT_THAT(ngx::That(s).IgnoreCase() == "ABC", Holds());
  EXPECT_THAT(ngx::That(s).IgnoreCase() != "ABC", Not(Holds()));
  EXPECT_THAT(ngx::That(s).IgnoreCase() == "ABD", Not(Holds()));
}

// Near() holds like CHECK_NEAR() if the operand lies within the margin.
TEST(ExperimentalEnsure, ThatNearHoldsWithinMargin) {
  EXPECT_THAT(ngx::That(5).Near(3, 2), Holds());
  EXPECT_THAT(ngx::That(1).Near(3, 2), Holds());
  EXPECT_THAT(ngx::That(6).Near(3, 2), Not(Holds()));
  EXPECT_THAT(ngx::That(0).Near(3, 2), Not(Holds()));
  EXPECT_THAT(ngx::That(1.05).Near(1.0, 0.1), Holds());
  EXPECT_THAT(ngx::That(1.2).Near(1.0, 0.1), Not(Holds()));
}

// Without a margin, Near() uses the one of CHECK_DOUBLE_EQ().
TEST(ExperimentalEnsure, ThatNearDefaultsToDoubleEqMargin) {
  constexpr double kTenth = 0.1;
  constexpr double kFifth = 0.2;
  constexpr double kSum = 0.3;
  constexpr double kLargeError = 1e-14;
  EXPECT_THAT(ngx::That(kTenth + kFifth).Near(kSum), Holds());
  EXPECT_THAT(ngx::That(kSum).Near(kSum + kLargeError), Not(Holds()));
  // The margin of CHECK_DOUBLE_EQ() is 1e-15, which lies between these.
  constexpr double kOne = 1.0;
  constexpr double kWithinMargin = 5e-16;
  constexpr double kBeyondMargin = 2e-15;
  EXPECT_THAT(ngx::That(kOne).Near(kOne + kWithinMargin), Holds());
  EXPECT_THAT(ngx::That(kOne).Near(kOne + kBeyondMargin), Not(Holds()));
}

// Like CHECK_NEAR(), NaN is never near anything.
TEST(ExperimentalEnsure, ThatNearRejectsNaN) {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  EXPECT_THAT(ngx::That(nan).Near(0.0, 1.0), Not(Holds()));
  EXPECT_THAT(ngx::That(0.0).Near(nan, 1.0), Not(Holds()));
}

// Integers are near by value regardless of their signedness, without the
// wraparound of unsigned arithmetic.
TEST(ExperimentalEnsure, ThatNearComparesIntegersByValue) {
  constexpr auto kMaxUnsigned = std::numeric_limits<std::uintmax_t>::max();
  EXPECT_THAT(ngx::That(0U).Near(1U, 2U), Holds());
  EXPECT_THAT(ngx::That(-1).Near(0U, 2U), Holds());
  EXPECT_THAT(ngx::That(-5).Near(-3, 2), Holds());
  EXPECT_THAT(ngx::That(5U).Near(1U, 2U), Not(Holds()));
  EXPECT_THAT(ngx::That(kMaxUnsigned).Near(-1, 1), Not(Holds()));
  EXPECT_THAT(ngx::That(1).Near(1, -1), Not(Holds()));
  // Distances beyond the range of std::intmax_t still fit std::uintmax_t.
  constexpr auto kTopBit = std::uintmax_t{1}
                           << (std::numeric_limits<std::uintmax_t>::digits - 1);
  EXPECT_THAT(ngx::That(kTopBit).Near(-1, kMaxUnsigned), Holds());
  EXPECT_THAT(ngx::That(-1).Near(kTopBit, kTopBit + 1), Holds());
}

// Mixing floating-point and integer operands computes the bounds in floating
// point instead of the integer types of the target and the margin, whose
// arithmetic could wrap around or overflow.
TEST(ExperimentalEnsure, ThatNearComputesMixedBoundsInFloatingPoint) {
  constexpr double kValue = 3e9;
  EXPECT_THAT(ngx::That(0.5).Near(0U, 1U), Holds());
  EXPECT_THAT(ngx::That(-0.5).Near(0, 1U), Holds());
  EXPECT_THAT(ngx::That(-kValue).Near(std::numeric_limits<int>::min(), 1),
              Not(Holds()));
}

// InRange() holds within the half-open range [low, high).
TEST(ExperimentalEnsure, ThatInRangeExcludesHighBound) {
  EXPECT_THAT(ngx::That(0).InRange(0, 3), Holds());
  EXPECT_THAT(ngx::That(2).InRange(0, 3), Holds());
  EXPECT_THAT(ngx::That(3).InRange(0, 3), Not(Holds()));
  EXPECT_THAT(ngx::That(-1).InRange(0, 3), Not(Holds()));
  EXPECT_THAT(ngx::That(0.5).InRange(0.0, 1.0), Holds());
}

// Integers compare by value, so a negative index is out of the range of an
// array like with CHECK_INDEX().
TEST(ExperimentalEnsure, ThatInRangeComparesIntegersByValue) {
  const int array[3] = {};
  const int negative = -1;
  EXPECT_THAT(ngx::That(negative).InRange(0, std::size(array)), Not(Holds()));
  EXPECT_THAT(ngx::That(2U).InRange(negative, 3), Holds());
}

// IndexOf() holds like CHECK_INDEX() for a valid index of a sized range.
TEST(ExperimentalEnsure, ThatIndexOfChecksSize) {
  const int array[3] = {};
  const std::vector<int> vector(2);
  const std::vector<int> empty;
  EXPECT_THAT(ngx::That(0).IndexOf(array), Holds());
  EXPECT_THAT(ngx::That(2).IndexOf(array), Holds());
  EXPECT_THAT(ngx::That(3).IndexOf(array), Not(Holds()));
  EXPECT_THAT(ngx::That(1U).IndexOf(vector), Holds());
  EXPECT_THAT(ngx::That(2U).IndexOf(vector), Not(Holds()));
  EXPECT_THAT(ngx::That(0).IndexOf(empty), Not(Holds()));
}

// Like with CHECK_INDEX(), a negative index is invalid.
TEST(ExperimentalEnsure, ThatIndexOfRejectsNegativeIndex) {
  const int array[3] = {};
  const int negative = -1;
  EXPECT_THAT(ngx::That(negative).IndexOf(array), Not(Holds()));
}

// BoundOf() holds like CHECK_BOUND() up to and including the size.
TEST(ExperimentalEnsure, ThatBoundOfIncludesSize) {
  const int array[3] = {};
  const std::vector<int> empty;
  const int negative = -1;
  EXPECT_THAT(ngx::That(0).BoundOf(array), Holds());
  EXPECT_THAT(ngx::That(3).BoundOf(array), Holds());
  EXPECT_THAT(ngx::That(4).BoundOf(array), Not(Holds()));
  EXPECT_THAT(ngx::That(0U).BoundOf(empty), Holds());
  EXPECT_THAT(ngx::That(negative).BoundOf(array), Not(Holds()));
}

#ifdef GTEST_HAS_DEATH_TEST
TEST_F(ExperimentalLogging, ThatNearFailureDescribesMargin) {
  EXPECT_DEATH(ngx::Ensure(ngx::That(1.5).Near(1.0, 0.1)),
               "Check failed: lhs near rhs \\(1\\.5 vs\\. 1 \\+/- 0\\.1\\)");
}

TEST_F(ExperimentalLogging, ThatIndexOfFailureDescribesSize) {
  const int array[3] = {};
  EXPECT_DEATH(ngx::Ensure(ngx::That(5).IndexOf(array)),
               "Check failed: lhs index of rhs \\(5 vs\\. size 3\\)");
}

TEST_F(ExperimentalLogging, ThatBoundOfFailureDescribesSize) {
  const int array[3] = {};
  EXPECT_DEATH(ngx::Ensure(ngx::That(5).BoundOf(array)),
               "Check failed: lhs bound of rhs \\(5 vs\\. size 3\\)");
}

TEST_F(ExperimentalLogging, ThatInRangeFailureDescribesRange) {
  EXPECT_DEATH(ngx::Ensure(ngx::That(5).InRange(0, 3), "msg"),
               "Check failed: lhs in range \\(5 vs\\. \\[0, 3\\)\\) msg");
}
#endif

// Comparing with nullptr checks like CHECK_NOTNULL() and returns the operand.
TEST_F(ExperimentalLogging, ThatNotNullReturnsLvalue) {
  const int value = 1;
  const int* const p = &value;
  const int* const& result = ngx::Ensure(ngx::That(p) != nullptr);
  EXPECT_EQ(&result, &p);
}

TEST_F(ExperimentalLogging, ThatNotNullMovesRvalue) {
  auto owned = std::make_unique<int>(1);
  const std::unique_ptr<int> result =
      ngx::Ensure(ngx::That(std::move(owned)) != nullptr, ngx::Debug);
  EXPECT_THAT(result, testing::Pointee(1));
}

// Like a null check, a comparison created by That() returns the operand, so
// the check can initialize members.
TEST_F(ExperimentalLogging, ThatComparisonReturnsLvalue) {
  const int value = 1;
  const int& result = ngx::Ensure(ngx::That(value) == 1);
  EXPECT_EQ(&result, &value);
}

TEST_F(ExperimentalLogging, ThatComparisonMovesRvalue) {
  std::string name = "abc";
  const std::string result =
      ngx::Ensure(ngx::That(std::move(name)) != std::string{}, "msg {}", 1);
  EXPECT_EQ(result, "abc");
  CopyCounter::copies = 0;
  [[maybe_unused]] const CopyCounter counter =
      ngx::Ensure(ngx::That(CopyCounter{}) == 1, "msg {}", 1);
  EXPECT_EQ(CopyCounter::copies, 0);
}

TEST_F(ExperimentalLogging, ThatComparisonDoesNotCopyRvalue) {
  CopyCounter::copies = 0;
  [[maybe_unused]] const CopyCounter result =
      ngx::Ensure(ngx::That(CopyCounter{}) == 1);
  EXPECT_EQ(CopyCounter::copies, 0);
}

TEST_F(ExperimentalLogging, ThatChecksReturnOperand) {
  const double x = 1.0;
  const int i = 1;
  const char* const s = "abc";
  const std::vector<int> range{1, 2};
  EXPECT_EQ(&ngx::Ensure(ngx::That(x).Near(1.0, 0.1), ngx::Debug), &x);
  EXPECT_EQ(&ngx::Ensure(ngx::That(x).Near(1.0)), &x);
  EXPECT_EQ(&ngx::Ensure(ngx::That(i).InRange(0, 2), ngx::Debug, "msg"), &i);
  EXPECT_EQ(&ngx::Ensure(ngx::That(i).IndexOf(range)), &i);
  EXPECT_EQ(&ngx::Ensure(ngx::That(i).BoundOf(range)), &i);
  EXPECT_EQ(&ngx::Ensure(ngx::That(s).IgnoreCase() == "ABC"), &s);
  EXPECT_EQ(&ngx::Ensure(ngx::That(s).AsPointer() == s), &s);
}

#ifdef GTEST_HAS_DEATH_TEST
TEST_F(ExperimentalLogging, ThatCStringFailureMatchesStrEq) {
  const char* const s = "abc";
  EXPECT_DEATH(ngx::Ensure(ngx::That(s) == "abd"),
               "CHECK_STREQ failed: lhs == rhs \\(abc vs\\. abd\\)");
  EXPECT_DEATH(ngx::Ensure(ngx::That(s).IgnoreCase() == "abd"),
               "CHECK_STRCASEEQ failed: lhs == rhs \\(abc vs\\. abd\\)");
}

// The null operand is streamed like the macros do, which renders it as 0, 0x0
// or 0000000000000000 depending on the standard library.
// A null C string is described by its address, not read as text.
TEST_F(ExperimentalLogging, ThatNotNullCstringFailureNamesOperands) {
  const char* const s = nullptr;
  EXPECT_DEATH(ngx::Ensure(ngx::That(s) != nullptr, "msg {}", 1),
               "Check failed: lhs != rhs \\(0x?0* vs\\. nullptr\\) msg 1");
}

// Like the numeric comparisons of That(), C string comparisons separate the
// message.
TEST_F(ExperimentalLogging, ThatCstringFailureSeparatesMessage) {
  const char* const s = "abc";
  EXPECT_DEATH(ngx::Ensure(ngx::That(s) == "abd", "msg {}", 3),
               "CHECK_STREQ failed: lhs == rhs \\(abc vs\\. abd\\) msg 3");
}

// NotNull() never prints the operand, so it accepts nullable types that can
// be neither streamed nor formatted.
TEST_F(ExperimentalLogging, NotNullAcceptsUnprintableHandle) {
  UnprintableHandle handle;
  EXPECT_DEATH(ngx::Ensure(ngx::NotNull(handle, "handle")),
               "Check failed: 'handle' Must be non nullptr");
}

// A smart pointer, unlike an object pointer, is rendered like any operand.
TEST_F(ExperimentalLogging, ThatNotNullFailureNamesOperands) {
  std::unique_ptr<int> p;
  EXPECT_DEATH(ngx::Ensure(ngx::That(std::move(p)) != nullptr, "msg {}", 1),
               "Check failed: lhs != rhs \\(0x?0* vs\\. nullptr\\) msg 1");
}

TEST_F(ExperimentalLogging, ThatFailureNamesOperatorAndOperands) {
  const int a = 1;
  EXPECT_DEATH(ngx::Ensure(ngx::That(a) == 2, "msg {}", 3),
               "Check failed: lhs == rhs \\(1 vs\\. 2\\) msg 3");
}
#endif

constexpr int kLeft = 1;
constexpr int kRight = 2;

// A failed comparison is printed like the failure message of Ensure().
TEST(ExperimentalEnsure, HoldsFailurePrintsDescription) {
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(NGLOG_COMPARE(kLeft == kRight), Holds()),
      "Expected: holds\n  Actual: kLeft == kRight (1 vs. 2) (of type "
      "nglog::experimental::Comparison)\n");
}

// A comparison that holds is printed as its expression text.
TEST(ExperimentalEnsure, NegatedHoldsFailurePrintsText) {
  EXPECT_NONFATAL_FAILURE(
      EXPECT_THAT(NGLOG_COMPARE(kLeft == 1), Not(Holds())),
      "Expected: does not hold\n  Actual: kLeft == 1 (of type "
      "nglog::experimental::Comparison)\n");
}

TEST_F(ExperimentalLogging, EnsureEqRendersCharactersLikeMacro) {
  const char c = 'a';
  const char newline = '\n';
  EXPECT_SAME_DEATH(CHECK_EQ(c, newline),
                    ngx::Ensure(ngx::Eq(c, newline, "c == newline"), ""));
}

TEST_F(ExperimentalLogging, EnsureEqRendersNullptrLikeMacro) {
  const int* const p = &kRepetitions;
  EXPECT_SAME_DEATH(CHECK_EQ(p, nullptr),
                    ngx::Ensure(ngx::Eq(p, nullptr, "p == nullptr"), ""));
}

// The comparisons CHECK_NEAR() expands to reproduce its output exactly, unlike
// Near(), which describes the failure as a single comparison.
TEST_F(ExperimentalLogging, EnsureBoundsMatchNearMacro) {
  const double x = 1.0;
  const double y = 1.5;
  const double margin = 0.25;
  EXPECT_SAME_DEATH(
      CHECK_NEAR(x, y, margin),
      (ngx::Ensure(ngx::Le((x), (y) + (margin), "(x) <= (y) + (margin)"), ""),
       ngx::Ensure(ngx::Ge((x), (y) - (margin), "(x) >= (y) - (margin)"), "")));
}

// The comparisons CHECK_DOUBLE_EQ() expands to reproduce its output exactly.
TEST_F(ExperimentalLogging, EnsureBoundsMatchDoubleEqMacro) {
  const double x = 1.0;
  const double y = 2.0;
  EXPECT_SAME_DEATH(CHECK_DOUBLE_EQ(x, y),
                    (ngx::Ensure(ngx::Le((x), (y) + 0.000000000000001L,
                                         "(x) <= (y) + 0.000000000000001L"),
                                 ""),
                     ngx::Ensure(ngx::Ge((x), (y)-0.000000000000001L,
                                         "(x) >= (y) - 0.000000000000001L"),
                                 "")));
}

TEST_F(ExperimentalLogging, EnsureStrEqMatchesMacro) {
  const char* const s1 = "abc";
  const char* const s2 = "abd";
  EXPECT_SAME_DEATH(CHECK_STREQ(s1, s2) << " msg",
                    ngx::Ensure(ngx::StrEq(s1, s2, "s1 == s2"), " msg"));
}

TEST_F(ExperimentalLogging, EnsureStrNeMatchesMacro) {
  const char* const s1 = "abc";
  EXPECT_SAME_DEATH(CHECK_STRNE(s1, "abc"),
                    ngx::Ensure(ngx::StrNe(s1, "abc", "s1 != \"abc\""), ""));
}

TEST_F(ExperimentalLogging, EnsureStrCaseEqMatchesMacro) {
  const char* const s1 = "abc";
  const char* const s2 = nullptr;
  EXPECT_SAME_DEATH(CHECK_STRCASEEQ(s1, s2),
                    ngx::Ensure(ngx::StrCaseEq(s1, s2, "s1 == s2"), ""));
}

TEST_F(ExperimentalLogging, EnsureStrCaseNeMatchesMacro) {
  const char* const s1 = "abc";
  EXPECT_SAME_DEATH(
      CHECK_STRCASENE(s1, "ABC"),
      ngx::Ensure(ngx::StrCaseNe(s1, "ABC", "s1 != \"ABC\""), ""));
}

TEST_F(ExperimentalLogging, EnsureNotNullMatchesMacro) {
  const int* const p = nullptr;
  EXPECT_SAME_DEATH(CHECK_NOTNULL(p), ngx::Ensure(ngx::NotNull(p, "p"), ""));
}

TEST_F(ExperimentalLogging, EnsureNotNullWithMoveOnlyMatchesMacro) {
  std::unique_ptr<int> u;
  EXPECT_SAME_DEATH(
      CHECK_NOTNULL(std::move(u)),
      ngx::Ensure(ngx::NotNull(std::move(u), "std::move(u)"), ""));
}

#ifdef GTEST_HAS_DEATH_TEST
TEST_F(ExperimentalLogging, EnsureNotNullAppendsMessage) {
  const int* const p = nullptr;
  EXPECT_DEATH(ngx::Ensure(ngx::NotNull(p, "p"), "msg {}", 1),
               "Check failed: 'p' Must be non nullptr msg 1");
}
#endif

TEST_F(ExperimentalLogging, EnsureNotNullReturnsLvalue) {
  int value = 1;
  int* p = &value;
  int*& checked = ngx::Ensure(ngx::NotNull(p, "p"), "");
  EXPECT_EQ(&checked, &p);
}

TEST_F(ExperimentalLogging, EnsureNotNullMovesRvalue) {
  constexpr int kValue = 3;
  auto u = std::make_unique<int>(kValue);
  const std::unique_ptr<int> v =
      ngx::Ensure(ngx::NotNull(std::move(u), "std::move(u)"), "");
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(*v, kValue);
  EXPECT_EQ(u, nullptr);
}

#if DCHECK_IS_ON()
TEST_F(ExperimentalLogging, DebugEnsureNotNullMatchesMacro) {
  const int* const p = nullptr;
  EXPECT_SAME_DEATH(DCHECK_NOTNULL(p),
                    ngx::Ensure(ngx::NotNull(p, "p"), ngx::Debug, ""));
}
#else
TEST_F(ExperimentalLogging, DebugEnsureNotNullReturnsValue) {
  const int* const p = nullptr;
  EXPECT_EQ(ngx::Ensure(ngx::NotNull(p, "p"), ngx::Debug, ""), nullptr);
}
#endif

TEST_F(ExperimentalLogging, LazyArgumentOfPassingEnsureIsNotEvaluated) {
  int evaluations = 0;
  const auto count = [&evaluations] { return ++evaluations; };
  ngx::Ensure(true, "{}", ngx::Lazy(count));
  ngx::Ensure(ngx::Eq(1, 1, "1 == 1"), "{}", ngx::Lazy(count));
  EXPECT_EQ(evaluations, 0);
}

TEST_F(ExperimentalLogging, PassingComparisonsAreSilent) {
  const Output output = Capture([] {
    ngx::Ensure(ngx::Eq(1, 1, "1 == 1"), "never");
    ngx::Ensure(ngx::Ne(1, 2, "1 != 2"), "never");
    ngx::Ensure(ngx::Lt(1, 2, "1 < 2"), "never");
    ngx::Ensure(ngx::Le(2, 2, "2 <= 2"), "never");
    ngx::Ensure(ngx::Gt(2, 1, "2 > 1"), "never");
    ngx::Ensure(ngx::Ge(2, 2, "2 >= 2"), "never");
    ngx::Ensure(ngx::StrEq("a", "a", "a == a"), "never");
    ngx::Ensure(ngx::StrNe("a", "b", "a != b"), "never");
    ngx::Ensure(ngx::StrCaseEq("a", "A", "a == A"), "never");
    ngx::Ensure(ngx::StrCaseNe("a", "b", "a != b"), "never");
  });
  EXPECT_THAT(output.stderr_text, IsEmpty());
}

#ifdef GTEST_HAS_DEATH_TEST
// No macro combines a comparison with errno, so the text is matched instead.
TEST_F(ExperimentalLogging, ComparisonAcceptsAttributes) {
  const int a = 1;
  EXPECT_DEATH(
      (errno = EACCES, ngx::Ensure(ngx::Eq(a, 2, "a == 2"), ngx::Errno, "x")),
      "Check failed: a == 2 \\(1 vs\\. 2\\) x: .* \\[13\\]");
}

TEST_F(ExperimentalLogging, ComparisonFormatsUnstreamableOperands) {
  EXPECT_DEATH(
      ngx::Ensure(ngx::Eq(Formattable{1}, Formattable{2}, "u == v"), ""),
      "Check failed: u == v \\(1 vs\\. 2\\) ");
}
#endif

#if DCHECK_IS_ON()
TEST_F(ExperimentalLogging, DebugComparisonMatchesMacro) {
  const int a = 1;
  EXPECT_SAME_DEATH(DCHECK_EQ(a, 2) << "debug",
                    ngx::Ensure(ngx::Eq(a, 2, "a == 2"), ngx::Debug, "debug"));
}
#else
TEST_F(ExperimentalLogging, DebugComparisonIsDisabled) {
  const Output output = Capture(
      [] { ngx::Ensure(ngx::Eq(1, 2, "1 == 2"), ngx::Debug, "debug"); });
  EXPECT_THAT(output.stderr_text, IsEmpty());
}
#endif

// Comparisons and null checks describe their failure, so the message is
// optional.
TEST_F(ExperimentalLogging, CompareWithoutMessageMatchesMacro) {
  const int a = 1;
  EXPECT_SAME_DEATH(CHECK_EQ(a, 2), ngx::Ensure(NGLOG_COMPARE(a == 2)));
}

TEST_F(ExperimentalLogging, NotNullWithoutMessageMatchesMacro) {
  const int* const p = nullptr;
  EXPECT_SAME_DEATH(CHECK_NOTNULL(p), ngx::Ensure(ngx::NotNull(p, "p")));
}

TEST_F(ExperimentalLogging, NotNullWithoutMessageReturnsOperand) {
  const int value = 1;
  EXPECT_EQ(ngx::Ensure(ngx::NotNull(&value, "&value")), &value);
}

#ifdef GTEST_HAS_DEATH_TEST
TEST_F(ExperimentalLogging, ThatWithoutMessageDescribesFailure) {
  const int a = 1;
  EXPECT_DEATH(ngx::Ensure(ngx::That(a) == 2),
               "Check failed: lhs == rhs \\(1 vs\\. 2\\)");
}

TEST_F(ExperimentalLogging, CompareWithoutMessageAcceptsAttributes) {
  const int a = 1;
  EXPECT_DEATH((errno = EACCES, ngx::Ensure(NGLOG_COMPARE(a == 2), ngx::Errno)),
               "Check failed: a == 2 \\(1 vs\\. 2\\) : .* \\[13\\]");
}
#endif

#if DCHECK_IS_ON()
TEST_F(ExperimentalLogging, DebugCompareWithoutMessageMatchesMacro) {
  const int a = 1;
  EXPECT_SAME_DEATH(DCHECK_EQ(a, 2),
                    ngx::Ensure(NGLOG_COMPARE(a == 2), ngx::Debug));
}

TEST_F(ExperimentalLogging, DebugNotNullWithoutMessageMatchesMacro) {
  const int* const p = nullptr;
  EXPECT_SAME_DEATH(DCHECK_NOTNULL(p),
                    ngx::Ensure(ngx::NotNull(p, "p"), ngx::Debug));
}
#else
TEST_F(ExperimentalLogging, DebugCompareWithoutMessageIsDisabled) {
  const Output output =
      Capture([] { ngx::Ensure(ngx::That(1) == 2, ngx::Debug); });
  EXPECT_THAT(output.stderr_text, IsEmpty());
}

TEST_F(ExperimentalLogging, DebugNotNullWithoutMessageReturnsValue) {
  const int* const p = nullptr;
  EXPECT_EQ(ngx::Ensure(ngx::NotNull(p, "p"), ngx::Debug), nullptr);
}
#endif

namespace constraints {

// Supports no comparison at all.
struct Unordered {};

template <class V, class U>
concept ThatEquality = requires(const V& v, const U& u) {
  ngx::That(v) == u;
  ngx::That(v) != u;
};

template <class V, class U>
concept StoredThatEquality =
    requires(decltype(ngx::That(std::declval<V>()))& operand, const U& u) {
      operand == u;
      operand != u;
    };

template <class V, class U>
concept ThatOrdering = requires(const V& v, const U& u) {
  ngx::That(v) < u;
  ngx::That(v) <= u;
  ngx::That(v) > u;
  ngx::That(v) >= u;
};

template <class V, class U, class M>
concept ThatNear =
    requires(const V& v, const U& u, const M& m) { ngx::That(v).Near(u, m); };

template <class V, class L, class H>
concept ThatInRange = requires(const V& v, const L& low, const H& high) {
  ngx::That(v).InRange(low, high);
};

template <class V, class R>
concept ThatIndexOf = requires(const V& v, const R& range) {
  ngx::That(v).IndexOf(range);
  ngx::That(v).BoundOf(range);
};

template <class V>
concept ThatIgnoreCase = requires(const V& v) { ngx::That(v).IgnoreCase(); };

template <class V>
concept ThatAsPointer = requires(const V& v) { ngx::That(v).AsPointer(); };

// Compares equal to nullptr like an empty optional without being a pointer.
struct OptionalLike {
  friend constexpr bool operator==(const OptionalLike& /*value*/,
                                   std::nullptr_t /*null*/) {
    return true;
  }
};

}  // namespace constraints
}  // namespace

// Prints OptionalLike, so that only the restriction to pointers can reject
// comparing it with nullptr.
template <>
struct std::formatter<constraints::OptionalLike> : std::formatter<int> {
  auto format(const constraints::OptionalLike& /*value*/,
              std::format_context& context) const {
    return std::formatter<int>::format(0, context);
  }
};

namespace {
namespace constraints {

static_assert(OptionalLike{} == nullptr,
              "OptionalLike compares with nullptr like an empty optional");

template <class V, class U>
concept ThatIgnoreCaseEquality =
    requires(const V& v, const U& u) { ngx::That(v).IgnoreCase() == u; };

template <class V, class U>
concept ThatIgnoreCaseOrdering =
    requires(const V& v, const U& u) { ngx::That(v).IgnoreCase() < u; };

// Compares equal without being printable, so a failure could not describe it.
struct Unprintable {
  int value;
  constexpr bool operator==(const Unprintable& other) const = default;
  constexpr auto operator<=>(const Unprintable& other) const = default;
};

// Misuse of That() is rejected by constraints, so that it can be detected.
static_assert(!ThatEquality<Unprintable, Unprintable>,
              "That() requires operands a failure can print");
static_assert(!ThatOrdering<Unprintable, Unprintable>,
              "That() requires operands a failure can print");
static_assert(!ThatInRange<Unprintable, Unprintable, Unprintable>,
              "That() requires operands a failure can print");
static_assert(ThatEquality<int, long>,
              "That() compares int with long for equality");
static_assert(!ThatEquality<Unordered, int>,
              "That() rejects comparing Unordered with int for equality");
static_assert(ThatOrdering<double, int>, "That() orders double and int");
static_assert(!ThatOrdering<Unordered, Unordered>,
              "That() rejects ordering Unordered and Unordered");
static_assert(ThatNear<double, double, double>,
              "Near() accepts an operand double with a target double and a "
              "margin double");
static_assert(!ThatNear<std::string, std::string, int>,
              "Near() rejects an operand std::string with a target std::string "
              "and a margin int");
static_assert(ThatInRange<int, unsigned, long>,
              "InRange() accepts an operand int with bounds unsigned and long");
static_assert(!ThatInRange<Unordered, int, int>,
              "InRange() rejects an operand Unordered with bounds int and int");
static_assert(ThatIndexOf<int, int[3]>,
              "IndexOf() and BoundOf() accept an index int of a range int[3]");
static_assert(ThatIndexOf<unsigned, std::vector<int>>,
              "IndexOf() and BoundOf() accept an index unsigned of a range "
              "std::vector<int>");
static_assert(
    !ThatIndexOf<double, int[3]>,
    "IndexOf() and BoundOf() reject an index double of a range int[3]");
static_assert(!ThatIndexOf<int, int>,
              "IndexOf() and BoundOf() reject an index int of a range int");
static_assert(ThatIgnoreCase<const char*>,
              "IgnoreCase() accepts the C string const char*");
static_assert(!ThatIgnoreCase<int>,
              "IgnoreCase() rejects int, which is no C string");
static_assert(!ThatIgnoreCase<std::string>,
              "IgnoreCase() rejects std::string, which is no C string");
static_assert(
    ThatIgnoreCaseEquality<const char*, const char*>,
    "IgnoreCase() compares the C strings const char* and const char*");
static_assert(!ThatIgnoreCaseEquality<const char*, std::string>,
              "IgnoreCase() rejects comparing const char* with std::string, "
              "which is no C string");
static_assert(!ThatIgnoreCaseOrdering<const char*, const char*>,
              "IgnoreCase() rejects ordering C strings");
// Comparing moves the operand into the result, so a stored operand must be
// moved to be compared once.
static_assert(!StoredThatEquality<int, int>,
              "a stored That() operand of int compares only once moved, since "
              "the comparison takes the operand");
static_assert(!StoredThatEquality<const int*, std::nullptr_t>,
              "a stored That() operand of const int* compares only once moved, "
              "since the comparison takes the operand");
// Only pointers are checked against null.
static_assert(ThatEquality<const int*, std::nullptr_t>,
              "That() compares const int* with std::nullptr_t for equality");
static_assert(
    !ThatEquality<OptionalLike, std::nullptr_t>,
    "That() rejects comparing OptionalLike with std::nullptr_t for equality");
static_assert(ThatAsPointer<const char*>,
              "AsPointer() accepts the object pointer const char*");
static_assert(ThatAsPointer<const wchar_t*>,
              "AsPointer() accepts the object pointer const wchar_t*");
static_assert(ThatAsPointer<int*>,
              "AsPointer() accepts the object pointer int*");
static_assert(!ThatAsPointer<int>,
              "AsPointer() rejects int, which is no object pointer");
static_assert(!ThatAsPointer<std::string>,
              "AsPointer() rejects std::string, which is no object pointer");

}  // namespace constraints

namespace constexpr_examples {

using namespace std::chrono_literals;

constexpr auto kComposed = ngx::Error | ngx::Tag{"net"} | ngx::Errno |
                           ngx::Verbose{1} | ngx::Every{1s};

// Logging is skipped and checks are evaluated during constant evaluation.
constexpr int Divide(int a, int b) {
  ngx::Log(kComposed, "dividing {} by {}", a, b);
  ngx::Log("dividing");
  ngx::Ensure(b != 0, "b != 0");
  ngx::Ensure(b != 0, ngx::Debug, "b != 0");
  ngx::Ensure(ngx::Ne(b, 0, "b != 0"), "");
  ngx::Ensure(ngx::Lt(a, 100, "a < 100"), ngx::Debug, "");
  ngx::Ensure(ngx::StrEq("x", "x", "x == x"), "");
  ngx::Ensure(ngx::StrCaseNe("x", "y", "x != y"), "");
  ngx::Ensure(ngx::That(b) != 0);
  ngx::Ensure(ngx::That(a) < 100, ngx::Debug);
  return a / b;
}

static_assert(Divide(6, 3) == 2,
              "Divide() logs and checks during constant evaluation");

constexpr int kValue = 4;

constexpr const int* Dereferenceable(const int* p) {
  return ngx::Ensure(ngx::NotNull(p, "p"), "");
}

static_assert(
    Dereferenceable(&kValue) == &kValue,
    "Ensure() returns the operand of NotNull() during constant evaluation");

constexpr const int* DereferenceableWithoutMessage(const int* p) {
  return ngx::Ensure(ngx::NotNull(p, "p"), ngx::Debug);
}

static_assert(DereferenceableWithoutMessage(&kValue) == &kValue,
              "Ensure() returns the operand of NotNull() without a message "
              "during constant evaluation");

constexpr int Positive(int value) { return ngx::Ensure(ngx::That(value) > 0); }

static_assert(
    Positive(kValue) == kValue,
    "Ensure() returns the operand of That() during constant evaluation");

constexpr int kIndexed[3] = {};

static_assert(ngx::Eq(1, 1, "1 == 1").holds(),
              "Eq(1, 1, \"1 == 1\") must hold during constant evaluation");
static_assert(NGLOG_COMPARE(1 == 1).holds(),
              "NGLOG_COMPARE(1 == 1) must hold during constant evaluation");
static_assert(!NGLOG_COMPARE(1 != 1).holds(),
              "NGLOG_COMPARE(1 != 1) must fail during constant evaluation");
static_assert(NGLOG_COMPARE(1 < 2).holds(),
              "NGLOG_COMPARE(1 < 2) must hold during constant evaluation");
static_assert(NGLOG_COMPARE(2 <= 2).holds(),
              "NGLOG_COMPARE(2 <= 2) must hold during constant evaluation");
static_assert(!NGLOG_COMPARE(1 > 2).holds(),
              "NGLOG_COMPARE(1 > 2) must fail during constant evaluation");
static_assert(!NGLOG_COMPARE(1 >= 2).holds(),
              "NGLOG_COMPARE(1 >= 2) must fail during constant evaluation");
static_assert(
    NGLOG_COMPARE(2 * 3 - 1 == 5).holds(),
    "NGLOG_COMPARE(2 * 3 - 1 == 5) must hold during constant evaluation");
static_assert((ngx::That(1) == 1).holds(),
              "That(1) == 1 must hold during constant evaluation");
static_assert(!(ngx::That(1) > 2).holds(),
              "That(1) > 2 must fail during constant evaluation");
static_assert((ngx::That(2 * 3 - 1) == 5).holds(),
              "That(2 * 3 - 1) == 5 must hold during constant evaluation");
static_assert((ngx::That("abc") == "abc").holds(),
              "That(\"abc\") == \"abc\" must hold during constant evaluation");
static_assert(!(ngx::That("abc") != "abc").holds(),
              "That(\"abc\") != \"abc\" must fail during constant evaluation");
static_assert((ngx::That("abc").IgnoreCase() == "ABC").holds(),
              "That(\"abc\").IgnoreCase() == \"ABC\" must hold during constant "
              "evaluation");
static_assert(ngx::That(5).Near(3, 2).holds(),
              "That(5).Near(3, 2) must hold during constant evaluation");
static_assert(!ngx::That(6).Near(3, 2).holds(),
              "That(6).Near(3, 2) must fail during constant evaluation");
// Mixed operands compute the bounds in floating point, also during constant
// evaluation.
static_assert(ngx::That(0.5).Near(0U, 1U).holds(),
              "That(0.5).Near(0U, 1U) must hold during constant evaluation");
static_assert(ngx::That(2).InRange(0, 3).holds(),
              "That(2).InRange(0, 3) must hold during constant evaluation");
static_assert(!ngx::That(3).InRange(0, 3).holds(),
              "That(3).InRange(0, 3) must fail during constant evaluation");
static_assert(ngx::That(2).IndexOf(kIndexed).holds(),
              "That(2).IndexOf(kIndexed) must hold during constant evaluation");
static_assert(!ngx::That(3).IndexOf(kIndexed).holds(),
              "That(3).IndexOf(kIndexed) must fail during constant evaluation");
static_assert(ngx::That(3).BoundOf(kIndexed).holds(),
              "That(3).BoundOf(kIndexed) must hold during constant evaluation");
static_assert(!ngx::That(4).BoundOf(kIndexed).holds(),
              "That(4).BoundOf(kIndexed) must fail during constant evaluation");
static_assert(!ngx::Eq(1, 2, "1 == 2").holds(),
              "Eq(1, 2, \"1 == 2\") must fail during constant evaluation");
// Like CHECK_STREQ(), null equals only null.
static_assert(ngx::StrEq(nullptr, nullptr, "null == null").holds(),
              "StrEq(nullptr, nullptr, \"null == null\") must hold during "
              "constant evaluation");
static_assert(!ngx::StrEq(nullptr, "", "null == empty").holds(),
              "StrEq(nullptr, \"\", \"null == empty\") must fail during "
              "constant evaluation");
static_assert(ngx::StrCaseEq("Abc", "aBC", "Abc == aBC").holds(),
              "StrCaseEq(\"Abc\", \"aBC\", \"Abc == aBC\") must hold during "
              "constant evaluation");
static_assert(!ngx::StrNe("abc", "abc", "abc != abc").holds(),
              "StrNe(\"abc\", \"abc\", \"abc != abc\") must fail during "
              "constant evaluation");
static_assert(!ngx::StrCaseNe("abc", "ABC", "abc != ABC").holds(),
              "StrCaseNe(\"abc\", \"ABC\", \"abc != ABC\") must fail during "
              "constant evaluation");

}  // namespace constexpr_examples

template <class A>
concept ComparisonCheckable = requires(const A& attributes) {
  ngx::Ensure(ngx::Eq(1, 2, "1 == 2"), attributes, "");
};

template <class A>
concept Checkable =
    requires(const A& attributes) { ngx::Ensure(false, attributes, ""); };

template <class A>
concept NotNullCheckable = requires(const A& attributes, int* p) {
  ngx::Ensure(ngx::NotNull(p, "p"), attributes, "");
};

template <class T>
concept NullCheckable =
    requires(T&& value) { ngx::NotNull(std::forward<T>(value), ""); };

// Only operands comparable with nullptr can be checked.
static_assert(NullCheckable<int*>,
              "NotNull() accepts the nullable pointer int*");
static_assert(NullCheckable<const char*&>,
              "NotNull() accepts the nullable pointer const char*&");
static_assert(NullCheckable<std::unique_ptr<int>>,
              "NotNull() accepts the nullable pointer std::unique_ptr<int>");
static_assert(NullCheckable<std::shared_ptr<int>&>,
              "NotNull() accepts the nullable pointer std::shared_ptr<int>&");
static_assert(NullCheckable<void (*)()>,
              "NotNull() accepts the nullable pointer void (*)()");
static_assert(
    !NullCheckable<int>,
    "NotNull() rejects int, which is no pointer comparable with nullptr");
static_assert(!NullCheckable<std::string>,
              "NotNull() rejects std::string, which is no pointer comparable "
              "with nullptr");
static_assert(!NullCheckable<std::optional<int*>>,
              "NotNull() rejects std::optional<int*>, which is no pointer "
              "comparable with nullptr");
static_assert(!NullCheckable<std::nullptr_t>,
              "NotNull() rejects std::nullptr_t, which is no pointer "
              "comparable with nullptr");
static_assert(NullCheckable<int Formattable::*>,
              "NotNull() accepts the nullable pointer int Formattable::*");
// The check is consumed by Ensure() and cannot be copied, which would allow
// checking one result twice.
static_assert(!std::is_copy_constructible_v<
                  decltype(ngx::NotNull(std::declval<int*>(), ""))>,
              "the result of NotNull(std::declval<int*>(), \"\") must not be "
              "copyable, so that it is checked once");

static_assert(NotNullCheckable<ngx::DebugOnly>,
              "a null check accepts DebugOnly");
static_assert(!NotNullCheckable<ngx::Severity>,
              "a null check rejects Severity, since checks are always fatal");
// Lvalues are returned by reference and rvalues by value like CHECK_NOTNULL().
static_assert(
    std::is_same_v<decltype(ngx::Ensure(ngx::NotNull(std::declval<int*&>(), ""),
                                        "")),
                   int*&>,
    "Ensure( NotNull(std::declval<int*&>(), \"\"), \"\") must return int*&");
static_assert(
    std::is_same_v<decltype(ngx::Ensure(ngx::NotNull(std::declval<int*>(), ""),
                                        "")),
                   int*>,
    "Ensure(NotNull(std::declval<int*>(), \"\"), \"\") must return int*");

// Like a null check, a comparison created by That() returns an lvalue operand
// by reference and an rvalue one by value.
static_assert(
    std::is_same_v<decltype(ngx::Ensure(ngx::That(std::declval<int&>()) == 1)),
                   int&>,
    "Ensure(That(std::declval<int&>()) == 1) must return int&");
static_assert(
    std::is_same_v<
        decltype(ngx::Ensure(ngx::That(std::declval<int>()) == 1, "")), int>,
    "Ensure(That(std::declval<int>()) == 1, \"\") must return int");
// Like a null check, the comparison cannot be copied, which would allow
// moving an rvalue operand out of it twice.
static_assert(!std::is_copy_constructible_v<decltype(ngx::That(1) == 1)>,
              "the result of That(1) == 1 must not be copyable, so that it is "
              "checked once");
static_assert(
    std::is_same_v<decltype(ngx::Ensure(ngx::That(std::declval<const char*&>())
                                            .AsPointer() == nullptr)),
                   const char*&>,
    "Ensure(That(std::declval<const char*&>()) .AsPointer() == nullptr) must "
    "return const char*&");

// Checks are always fatal, so they reject a severity.
static_assert(ComparisonCheckable<ngx::DebugOnly>,
              "a comparison check accepts DebugOnly");
static_assert(ComparisonCheckable<decltype(ngx::Debug | ngx::Errno)>,
              "a comparison check accepts Debug | Errno");
static_assert(
    !ComparisonCheckable<ngx::Severity>,
    "a comparison check rejects Severity, since checks are always fatal");
static_assert(
    !ComparisonCheckable<decltype(ngx::Debug | ngx::Error)>,
    "a comparison check rejects Debug | Error, since checks are always fatal");
static_assert(Checkable<ngx::ErrnoDescription>,
              "a condition check accepts ErrnoDescription");
static_assert(
    !Checkable<ngx::Severity>,
    "a condition check rejects Severity, since checks are always fatal");

}  // namespace
