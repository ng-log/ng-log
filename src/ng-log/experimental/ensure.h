// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Ensure() terminates the program unless a condition, a comparison like the
// CHECK_EQ() family or a null check like CHECK_NOTNULL() holds.

#ifndef NGLOG_EXPERIMENTAL_ENSURE_H
#define NGLOG_EXPERIMENTAL_ENSURE_H

#include "ng-log/experimental/requirements.h"  // IWYU pragma: keep

#if defined(NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_SUPPORTED)

#  include <concepts>
#  include <cstddef>
#  include <cstdint>
#  include <format>
#  include <memory>
#  include <optional>
#  include <ranges>
#  include <source_location>
#  include <sstream>
#  include <string>
#  include <string_view>
#  include <type_traits>
#  include <utility>

#  include "ng-log/experimental/attributes.h"
#  include "ng-log/experimental/format.h"
#  include "ng-log/experimental/log_call.h"
#  include "ng-log/export.h"
#  include "ng-log/log_message.h"

namespace nglog::experimental {

namespace internal {

// How the description of a failed check precedes the message.
enum class EnsureFailureKind {
  // "Check failed: " like CHECK().
  kCondition,
  // "Check failed: <description> " like CHECK_EQ().
  kComparison,
  // "<description>" like CHECK_STREQ().
  kStringComparison,
  // "<description> " like the other comparisons of That().
  kSeparatedStringComparison,
};

// Precedes the message of a failed condition like CHECK().
inline constexpr std::string_view kCheckFailedPrefix = "Check failed: ";

// Marks a message as a failed check.
struct EnsureFailure {
  using Category = SeverityCategory;

  // Must outlive the logging call.
  std::string_view description;
  EnsureFailureKind kind = EnsureFailureKind::kCondition;

  NGLOG_EXPORT void Configure(Context& context) const;
  NGLOG_EXPORT void Prefix(Message& message) const;
};

template <class T>
concept Streamable =
    requires(std::ostream& stream, const T& value) { stream << value; };

struct ComparisonFactory;

template <class T>
inline constexpr bool kEnsurable = Compatible<EnsureFailure, T>;

template <class... As>
inline constexpr bool kEnsurable<Attributes<As...>> =
    Compatible<EnsureFailure, As...>;

}  // namespace internal

// Result of comparing two operands for Ensure(). A failed comparison describes
// the expression text and both operands like the CHECK_EQ() family does.
class [[nodiscard("Pass the comparison to Ensure().")]] Comparison {
 public:
  constexpr bool holds() const noexcept { return !failed_; }

  // Rejects a comma, such as in NGLOG_COMPARE(a == b, "msg"), which would
  // discard the comparison.
  template <class U>
  void operator,(const U& other) const = delete;

 private:
  friend struct internal::ComparisonFactory;

  constexpr explicit Comparison(const char* text) noexcept : text_{text} {}
  constexpr Comparison(const char* text, std::string failure,
                       internal::EnsureFailureKind kind) noexcept
      : text_{text}, failure_{std::move(failure)}, kind_{kind}, failed_{true} {}

  const char* text_;
  std::string failure_;
  internal::EnsureFailureKind kind_ = internal::EnsureFailureKind::kComparison;
  bool failed_ = false;
};

namespace internal {

struct ComparisonFactory {
  // Describes a failed comparison as "<text> (<v1> vs. <v2>)" exactly like the
  // macros do if both operands support stream insertion. Otherwise, each
  // operand is rendered on its own. During constant evaluation, which cannot
  // render operands, the description is the expression text.
  template <class T1, class T2>
  static constexpr Comparison Make(bool holds, const T1& v1, const T2& v2,
                                   const char* text) {
    if (holds) [[likely]] {
      return Comparison{text};
    }
    if (std::is_constant_evaluated()) {
      return Comparison{text, std::string{text},
                        EnsureFailureKind::kComparison};
    }
    if constexpr (Streamable<T1> && Streamable<T2>) {
      return Comparison{
          text, std::move(*::nglog::internal::MakeCheckOpString(v1, v2, text)),
          EnsureFailureKind::kComparison};
    } else {
      return Comparison{text,
                        std::format("{} ({} vs. {})", std::string_view{text},
                                    Render(v1), Render(v2)),
                        EnsureFailureKind::kComparison};
    }
  }

  // Renders an operand like the macros do if it supports stream insertion and
  // using std::format otherwise.
  template <class T>
  static std::string Render(const T& value) {
    if constexpr (Streamable<T>) {
      std::ostringstream stream;
      ::nglog::internal::MakeCheckOpValueString(&stream, value);
      return std::move(stream).str();
    } else {
      return std::format("{}", value);
    }
  }

  // Describes a failed comparison by calling describe(), which runs only on
  // failure. During constant evaluation, the description is the text.
  template <class Describe>
  static constexpr Comparison MakeDescribed(bool holds, const char* text,
                                            Describe describe) {
    if (holds) [[likely]] {
      return Comparison{text};
    }
    if (std::is_constant_evaluated()) {
      return Comparison{text, std::string{text},
                        EnsureFailureKind::kComparison};
    }
    return Comparison{text, describe(), EnsureFailureKind::kComparison};
  }

  // Adopts the description of a failed string comparison or null.
  static Comparison Make(std::unique_ptr<std::string> failure,
                         const char* text) {
    if (failure == nullptr) {
      return Comparison{text};
    }
    return Comparison{text, std::move(*failure),
                      EnsureFailureKind::kStringComparison};
  }

  // Separates the message from the description of a failed string comparison
  // like the other comparisons of That().
  static constexpr Comparison Separated(Comparison comparison) noexcept {
    if (comparison.failed_ &&
        comparison.kind_ == EnsureFailureKind::kStringComparison) {
      comparison.kind_ = EnsureFailureKind::kSeparatedStringComparison;
    }
    return comparison;
  }

  // Describes a string comparison during constant evaluation.
  static constexpr Comparison MakeConstant(bool holds, const char* text) {
    if (holds) {
      return Comparison{text};
    }
    return Comparison{text, std::string{text},
                      EnsureFailureKind::kStringComparison};
  }

  // Expression text of a comparison.
  static constexpr std::string_view Text(
      const Comparison& comparison) noexcept {
    return comparison.text_;
  }

  // Description of a failed comparison as Ensure() reports it, or empty.
  static constexpr std::string_view Description(
      const Comparison& comparison) noexcept {
    return comparison.failure_;
  }

  static constexpr EnsureFailure Failure(
      const Comparison& comparison) noexcept {
    return EnsureFailure{comparison.failure_, comparison.kind_};
  }
};

constexpr char ToLowerAscii(char c) noexcept {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// Compares C strings during constant evaluation like the CHECK_STREQ() family
// does at runtime: null equals only null. Ignoring case is limited to ASCII.
constexpr bool ConstantStringsEqual(const char* s1, const char* s2,
                                    bool ignore_case) noexcept {
  if (s1 == nullptr || s2 == nullptr) {
    return s1 == nullptr && s2 == nullptr;
  }
  for (;; ++s1, ++s2) {
    const char c1 = ignore_case ? ToLowerAscii(*s1) : *s1;
    const char c2 = ignore_case ? ToLowerAscii(*s2) : *s2;
    if (c1 != c2) {
      return false;
    }
    if (c1 == '\0') {
      return true;
    }
  }
}

// Never constexpr, so that calling it while evaluating a constant expression
// names the reason the evaluation fails.
inline void EnsureFailedDuringConstantEvaluation() noexcept {}

template <class T>
inline constexpr bool kDebugOnly = std::is_same_v<T, DebugOnly>;

template <class... As>
inline constexpr bool kDebugOnly<Attributes<As...>> = (kDebugOnly<As> || ...);

// Fails constant evaluation unless debug checks are disabled for the check.
template <bool DebugMode, class A>
constexpr void FailConstantEvaluation() noexcept {
  if (DebugMode || !kDebugOnly<A>) {
    EnsureFailedDuringConstantEvaluation();
  }
}

// Integers that compare by value regardless of their signedness, including
// characters, whose built-in comparison converts a negative value to unsigned
// as well.
template <class T>
concept ValueInteger = std::is_integral_v<T> && !std::is_same_v<T, bool>;

// Converts an integer to the standard integer of the same signedness and size,
// which std::cmp_less() and its siblings accept unlike characters.
template <ValueInteger T>
constexpr auto IntegerValue(T value) noexcept {
  if constexpr (StandardInteger<T>) {
    return value;
  } else if constexpr (std::is_signed_v<T>) {
    return static_cast<std::make_signed_t<T>>(value);
  } else {
    return static_cast<std::make_unsigned_t<T>>(value);
  }
}

// Compares integers by value regardless of their signedness and other operands
// using their operator.
template <class T1, class T2, class Operator, class IntegerOperator>
constexpr Comparison Compare(const T1& v1, const T2& v2, const char* text,
                             Operator op, IntegerOperator integer_op) {
  if constexpr (ValueInteger<T1> && ValueInteger<T2>) {
    return ComparisonFactory::Make(
        integer_op(IntegerValue(v1), IntegerValue(v2)), v1, v2, text);
  } else {
    return ComparisonFactory::Make(op(v1, v2), v1, v2, text);
  }
}

// Orders integers by value regardless of their signedness and other operands
// using their operators.
template <class T1, class T2>
constexpr bool LessByValue(const T1& v1, const T2& v2) {
  if constexpr (ValueInteger<T1> && ValueInteger<T2>) {
    return std::cmp_less(IntegerValue(v1), IntegerValue(v2));
  } else {
    return v1 < v2;
  }
}

template <class T1, class T2>
constexpr bool LessEqualByValue(const T1& v1, const T2& v2) {
  if constexpr (ValueInteger<T1> && ValueInteger<T2>) {
    return std::cmp_less_equal(IntegerValue(v1), IntegerValue(v2));
  } else {
    return v1 <= v2;
  }
}

// Distance between integers whose first is not less than the second by value,
// or nothing if it exceeds the range of std::uintmax_t.
template <class H, class L>
constexpr std::optional<std::uintmax_t> IntegerDistance(H high,
                                                        L low) noexcept {
  // Unsigned arithmetic wraps around to the distance modulo the range of
  // std::uintmax_t.
  const auto distance =
      static_cast<std::uintmax_t>(high) - static_cast<std::uintmax_t>(low);
  // The distance can exceed the range only if the first is not negative and
  // the second is. The magnitude of the second then adds to the first, so a
  // distance that wrapped around is less than the first.
  if (std::cmp_greater_equal(high, 0) && std::cmp_less(low, 0) &&
      distance < static_cast<std::uintmax_t>(high)) {
    return std::nullopt;
  }
  return distance;
}

// Whether integers lie within a margin of each other by value, without the
// wraparound of unsigned arithmetic.
template <class T, class U, class M>
constexpr bool IntegersNear(T value, U target, M margin) noexcept {
  // A distance is never negative, so a negative margin never holds.
  const std::optional<std::uintmax_t> distance =
      std::cmp_greater_equal(value, target) ? IntegerDistance(value, target)
                                            : IntegerDistance(target, value);
  return distance.has_value() && std::cmp_less_equal(*distance, margin);
}

// Margin of CHECK_DOUBLE_EQ().
inline constexpr long double kDoubleEqualMargin = 0.000000000000001L;

}  // namespace internal

// Compares like CHECK_EQ(). text is the expression, such as "a == b". Unlike
// with the macros, integers of mixed signedness compare by value.
template <class T1, class T2>
constexpr Comparison Eq(const T1& v1, const T2& v2, const char* text) {
  return internal::Compare(
      v1, v2, text, [](const auto& a, const auto& b) { return a == b; },
      [](auto a, auto b) { return std::cmp_equal(a, b); });
}

// Compares like CHECK_NE().
template <class T1, class T2>
constexpr Comparison Ne(const T1& v1, const T2& v2, const char* text) {
  return internal::Compare(
      v1, v2, text, [](const auto& a, const auto& b) { return a != b; },
      [](auto a, auto b) { return std::cmp_not_equal(a, b); });
}

// Compares like CHECK_LT().
template <class T1, class T2>
constexpr Comparison Lt(const T1& v1, const T2& v2, const char* text) {
  return internal::Compare(
      v1, v2, text, [](const auto& a, const auto& b) { return a < b; },
      [](auto a, auto b) { return std::cmp_less(a, b); });
}

// Compares like CHECK_LE().
template <class T1, class T2>
constexpr Comparison Le(const T1& v1, const T2& v2, const char* text) {
  return internal::Compare(
      v1, v2, text, [](const auto& a, const auto& b) { return a <= b; },
      [](auto a, auto b) { return std::cmp_less_equal(a, b); });
}

// Compares like CHECK_GT().
template <class T1, class T2>
constexpr Comparison Gt(const T1& v1, const T2& v2, const char* text) {
  return internal::Compare(
      v1, v2, text, [](const auto& a, const auto& b) { return a > b; },
      [](auto a, auto b) { return std::cmp_greater(a, b); });
}

// Compares like CHECK_GE().
template <class T1, class T2>
constexpr Comparison Ge(const T1& v1, const T2& v2, const char* text) {
  return internal::Compare(
      v1, v2, text, [](const auto& a, const auto& b) { return a >= b; },
      [](auto a, auto b) { return std::cmp_greater_equal(a, b); });
}

// Compares C strings like CHECK_STREQ(). Null equals only null.
constexpr Comparison StrEq(const char* s1, const char* s2, const char* text) {
  if (std::is_constant_evaluated()) {
    return internal::ComparisonFactory::MakeConstant(
        internal::ConstantStringsEqual(s1, s2, false), text);
  }
  return internal::ComparisonFactory::Make(
      ::nglog::internal::CheckstrcmptrueImpl(s1, s2, text), text);
}

// Compares C strings like CHECK_STRNE().
constexpr Comparison StrNe(const char* s1, const char* s2, const char* text) {
  if (std::is_constant_evaluated()) {
    return internal::ComparisonFactory::MakeConstant(
        !internal::ConstantStringsEqual(s1, s2, false), text);
  }
  return internal::ComparisonFactory::Make(
      ::nglog::internal::CheckstrcmpfalseImpl(s1, s2, text), text);
}

// Compares C strings ignoring case like CHECK_STRCASEEQ().
constexpr Comparison StrCaseEq(const char* s1, const char* s2,
                               const char* text) {
  if (std::is_constant_evaluated()) {
    return internal::ComparisonFactory::MakeConstant(
        internal::ConstantStringsEqual(s1, s2, true), text);
  }
  return internal::ComparisonFactory::Make(
      ::nglog::internal::CheckstrcasecmptrueImpl(s1, s2, text), text);
}

// Compares C strings ignoring case like CHECK_STRCASENE().
constexpr Comparison StrCaseNe(const char* s1, const char* s2,
                               const char* text) {
  if (std::is_constant_evaluated()) {
    return internal::ComparisonFactory::MakeConstant(
        !internal::ConstantStringsEqual(s1, s2, true), text);
  }
  return internal::ComparisonFactory::Make(
      ::nglog::internal::CheckstrcasecmpfalseImpl(s1, s2, text), text);
}

namespace internal {

template <class T>
inline constexpr bool kDependentFalse = false;

// Left operand of the comparison NGLOG_COMPARE() decomposes. Comparing it
// forwards both operands and the expression text to Eq() and its siblings.
// Arithmetic binds tighter than comparisons, so it applies to the value and
// yields the operand of the result.
template <class T>
class [[nodiscard("Compare the operand and pass the comparison to Ensure().")]]
ComparisonOperand {
 public:
  constexpr ComparisonOperand(T value, const char* text) noexcept
      : value_{static_cast<T&&>(value)}, text_{text} {}

  template <class U>
  constexpr auto operator+(const U& other) const {
    return ComparisonOperand<decltype(value_ + other)>{value_ + other, text_};
  }

  template <class U>
  constexpr auto operator-(const U& other) const {
    return ComparisonOperand<decltype(value_ - other)>{value_ - other, text_};
  }

  template <class U>
  constexpr auto operator*(const U& other) const {
    return ComparisonOperand<decltype(value_ * other)>{value_ * other, text_};
  }

  template <class U>
  constexpr auto operator/(const U& other) const {
    return ComparisonOperand<decltype(value_ / other)>{value_ / other, text_};
  }

  template <class U>
  constexpr auto operator%(const U& other) const {
    return ComparisonOperand<decltype(value_ % other)>{value_ % other, text_};
  }

  template <class U>
  constexpr Comparison operator==(const U& other) const {
    return Eq(value_, other, text_);
  }

  template <class U>
  constexpr Comparison operator!=(const U& other) const {
    return Ne(value_, other, text_);
  }

  template <class U>
  constexpr Comparison operator<(const U& other) const {
    return Lt(value_, other, text_);
  }

  template <class U>
  constexpr Comparison operator<=(const U& other) const {
    return Le(value_, other, text_);
  }

  template <class U>
  constexpr Comparison operator>(const U& other) const {
    return Gt(value_, other, text_);
  }

  template <class U>
  constexpr Comparison operator>=(const U& other) const {
    return Ge(value_, other, text_);
  }

  // Rejects a comma, such as in NGLOG_COMPARE(a, b), which would compare only
  // the last operand.
  template <class U>
  void operator,(const U& other) const = delete;

  // Rejects an expression without a top-level comparison.
  template <class U = T>
  constexpr operator Comparison() const {
    static_assert(kDependentFalse<U>,
                  "NGLOG_COMPARE() expects a comparison such as a == b");
    return ComparisonFactory::MakeConstant(false, text_);
  }

 private:
  T value_;
  const char* text_;
};

// Captures the left operand of the expression passed to NGLOG_COMPARE(). The
// operator ->* binds tighter than any other binary operator, so it applies to
// the left operand only. Unlike operators binding weaker than arithmetic, it
// does not combine with the comparison into what compilers and linters warn
// about as chained comparisons or shifts.
struct ComparisonDecomposer {
  const char* text;

  template <class T>
  friend constexpr ComparisonOperand<const T&> operator->*(
      ComparisonDecomposer decomposer, const T& value) noexcept {
    return {value, decomposer.text};
  }
};

}  // namespace internal

namespace internal {

template <class T>
inline constexpr bool kPointerLike =
    std::is_pointer_v<T> || std::is_member_pointer_v<T> ||
    requires { typename T::element_type; };

// Raw, member and smart pointers that compare with nullptr. Wrappers such as
// std::optional are rejected even though they compare with nullptr, since an
// empty one compares unequal and would pass the check.
template <class T>
concept NullablePointer = kPointerLike<std::remove_cvref_t<T>> &&
                          requires(const std::remove_reference_t<T>& value) {
                            { value == nullptr } -> std::convertible_to<bool>;
                          };

struct NotNullAccess;
struct OperandAccess;

// Pointers AsPointer() compares by address, such as C strings.
template <class T>
concept ObjectPointer =
    std::is_pointer_v<std::decay_t<T>> &&
    std::is_object_v<std::remove_pointer_t<std::decay_t<T>>> &&
    !std::is_volatile_v<std::remove_pointer_t<std::decay_t<T>>>;

// Operands that ComparisonFactory::Render() can render.
template <class T>
concept Renderable =
    Streamable<T> || std::is_default_constructible_v<
                         std::formatter<std::remove_cvref_t<T>, char>>;

}  // namespace internal

// Operand of a null check created by NotNull(). Holds a reference to an lvalue
// and takes ownership of an rvalue, so it must not outlive the object an lvalue
// operand refers to.
template <class T>
class [[nodiscard("Pass the null check to Ensure().")]] NotNullCheck {
 public:
  NotNullCheck(const NotNullCheck& other) = delete;
  NotNullCheck& operator=(const NotNullCheck& other) = delete;

 private:
  friend struct internal::NotNullAccess;

  constexpr NotNullCheck(T&& value, const char* text) noexcept(
      std::is_nothrow_constructible_v<T, T&&>)
      : value_{std::forward<T>(value)}, text_{text} {}

  T value_;
  const char* text_;
};

namespace internal {

struct NotNullAccess {
  template <class T>
  static constexpr NotNullCheck<T> Make(T&& value, const char* text) {
    return NotNullCheck<T>{std::forward<T>(value), text};
  }

  template <class T>
  static constexpr bool IsNull(const NotNullCheck<T>& check) {
    return check.value_ == nullptr;
  }

  // Describes the failure like CHECK_NOTNULL(), or like the comparison with
  // nullptr for That(), which provides no text.
  template <class T>
  static constexpr std::string Describe(const NotNullCheck<T>& check) {
    if (check.text_ == nullptr) {
      return DescribeComparison(check.value_);
    }
    return std::string{"'"} + check.text_ + "' Must be non nullptr";
  }

  // Describes the failed comparison of That(p) != nullptr. A raw pointer is
  // rendered as an address, since a null C string must not be read. An operand
  // that cannot be rendered is omitted, so that NotNull() accepts it.
  template <class V>
  static constexpr std::string DescribeComparison(const V& value) {
    constexpr const char* text = "lhs != rhs";
    if constexpr (ObjectPointer<V>) {
      return std::string{ComparisonFactory::Description(ComparisonFactory::Make(
          false, static_cast<const void*>(value), nullptr, text))};
    } else if constexpr (Renderable<V>) {
      return std::string{ComparisonFactory::Description(
          ComparisonFactory::Make(false, value, nullptr, text))};
    } else {
      return text;
    }
  }

  template <class T>
  static constexpr T Release(NotNullCheck<T>&& check) {
    return std::forward<T>(check.value_);
  }
};

}  // namespace internal

// Checks like CHECK_NOTNULL() that value is not null. text is the
// expression, such as "p". Passing the result to Ensure() returns value, an
// lvalue by reference and an rvalue by value.
template <class T>
  requires internal::NullablePointer<T>
constexpr NotNullCheck<T> NotNull(T&& value, const char* text) {
  return internal::NotNullAccess::Make(std::forward<T>(value), text);
}

namespace internal {

// C strings That() compares by their contents, such as const char* and char
// arrays.
template <class T>
concept Cstring = std::same_as<std::decay_t<T>, char*> ||
                  std::same_as<std::decay_t<T>, const char*>;

// Character types other than char, whose pointers are wide strings or byte
// buffers that may not be terminated.
template <class T>
inline constexpr bool kOtherCharacter =
    std::is_same_v<T, signed char> || std::is_same_v<T, unsigned char> ||
    std::is_same_v<T, wchar_t> || std::is_same_v<T, char8_t> ||
    std::is_same_v<T, char16_t> || std::is_same_v<T, char32_t>;

// Pointers to other character types, which the string comparisons do not
// support and which would compare by address but print as text.
template <class T>
concept OtherCstring =
    std::is_pointer_v<std::decay_t<T>> &&
    kOtherCharacter<std::remove_cv_t<std::remove_pointer_t<std::decay_t<T>>>>;

template <class T>
concept AnyCstring = Cstring<T> || OtherCstring<T>;

// Operands That() compares using == and !=. Arrays are compared as pointers,
// since C++26 rejects comparing two arrays.
template <class T, class U>
concept Equatable =
    requires(const std::decay_t<T>& lhs, const std::decay_t<U>& rhs) {
      { lhs == rhs } -> std::convertible_to<bool>;
      { lhs != rhs } -> std::convertible_to<bool>;
    };

// Operands That() orders using <, <=, > and >=.
template <class T, class U>
concept Orderable =
    requires(const std::decay_t<T>& lhs, const std::decay_t<U>& rhs) {
      { lhs < rhs } -> std::convertible_to<bool>;
      { lhs <= rhs } -> std::convertible_to<bool>;
      { lhs > rhs } -> std::convertible_to<bool>;
      { lhs >= rhs } -> std::convertible_to<bool>;
    };

// Operands Near() compares within a margin like CHECK_NEAR().
template <class T, class U, class M>
concept NearComparable =
    requires(const T& value, const U& target, const M& margin) {
      { value <= target + margin } -> std::convertible_to<bool>;
      { value >= target - margin } -> std::convertible_to<bool>;
    };

// Operands IndexOf() and BoundOf() compare with the size of a range.
template <class T>
concept IntegerOperand = StandardInteger<std::remove_cvref_t<T>>;

// How That() compares its operand.
enum class ThatMode {
  // By value, C strings by their contents.
  kValue,
  // C strings by their contents ignoring case.
  kIgnoreCase,
  // Pointers by address.
  kAddress,
};

}  // namespace internal

// Result of a comparison created by That(). Passing it to Ensure() returns the
// operand, an lvalue by reference and an rvalue by value. Holds a reference to
// an lvalue operand and owns an rvalue one like That().
template <class T>
class [[nodiscard("Pass the comparison to Ensure().")]] OperandComparison
    : public Comparison {
 public:
  OperandComparison(const OperandComparison& other) = delete;
  OperandComparison& operator=(const OperandComparison& other) = delete;

 private:
  friend struct internal::OperandAccess;

  constexpr OperandComparison(Comparison comparison, T&& value) noexcept(
      std::is_nothrow_constructible_v<T, T&&>)
      : Comparison{std::move(comparison)}, value_{std::forward<T>(value)} {}

  T value_;
};

namespace internal {

struct OperandAccess {
  template <class T>
  static constexpr OperandComparison<T> Make(Comparison comparison, T&& value) {
    return OperandComparison<T>{std::move(comparison), std::forward<T>(value)};
  }

  template <class T>
  static constexpr T Release(OperandComparison<T>&& comparison) {
    return std::forward<T>(comparison.value_);
  }
};

// Type a right operand compared with an operand of type Compared prints as.
// C strings compared with an untyped pointer print as addresses as well.
template <class Compared, class U>
using PeerOf =
    std::conditional_t<std::same_as<std::decay_t<Compared>, const void*> &&
                           AnyCstring<U>,
                       const void*, std::decay_t<U>>;

// Left operand of a comparison created by That(). Holds a reference to an
// lvalue and takes ownership of an rvalue, so a stored operand must not outlive
// the object an lvalue operand refers to. C strings compare by their contents,
// and comparing a pointer with nullptr checks like CHECK_NOTNULL(). Comparing
// the operand moves it into the result, so the operand compares only once.
template <class T, ThatMode kMode = ThatMode::kValue>
class [[nodiscard("Compare the operand and pass the comparison to Ensure().")]]
ThatOperand {
  // Whether the operand compares and prints as an address. Pointers to other
  // character types need not point to terminated strings, so they are never
  // read as text.
  static constexpr bool kByAddress =
      kMode == ThatMode::kAddress || OtherCstring<T>;

  // Whether the operand is a C string that That() rejects comparing with
  // another one by content or by order.
  static constexpr bool kTextual = kMode != ThatMode::kAddress && AnyCstring<T>;

  // The type the operand compares as.
  using Compared = std::conditional_t<kByAddress, const void*, T>;

 public:
  // Takes an rvalue reference, so that an rvalue is moved only once.
  constexpr explicit ThatOperand(T&& value) noexcept(
      std::is_nothrow_constructible_v<T, T&&>)
      : value_{static_cast<T&&>(value)} {}

  // Compares C strings ignoring case like CHECK_STRCASEEQ().
  constexpr ThatOperand<T, ThatMode::kIgnoreCase> IgnoreCase() &&
    requires(kMode == ThatMode::kValue && Cstring<T>)
  {
    return ThatOperand<T, ThatMode::kIgnoreCase>{static_cast<T&&>(value_)};
  }

  // Compares the operand by address, also a C string.
  constexpr ThatOperand<T, ThatMode::kAddress> AsPointer() &&
    requires(kMode == ThatMode::kValue && ObjectPointer<T>)
  {
    return ThatOperand<T, ThatMode::kAddress>{static_cast<T&&>(value_)};
  }

  template <class U>
    requires Equatable<Compared, U> && Renderable<Compared> &&
             Renderable<PeerOf<Compared, U>> &&
             (kMode != ThatMode::kIgnoreCase || Cstring<U>) &&
             (!std::same_as<U, std::nullptr_t> || NullablePointer<T>)
  constexpr OperandComparison<T> operator==(const U& other) && {
    return std::move(*this).Keep(Equal<true>(other));
  }

  template <class U>
    requires Equatable<Compared, U> && Renderable<Compared> &&
             Renderable<PeerOf<Compared, U>> &&
             (kMode != ThatMode::kIgnoreCase || Cstring<U>) &&
             (!std::same_as<U, std::nullptr_t> || NullablePointer<T>)
  constexpr OperandComparison<T> operator!=(const U& other) && {
    return std::move(*this).Keep(Equal<false>(other));
  }

  // Checks like CHECK_NOTNULL(). Passing the result to Ensure() returns the
  // operand.
  constexpr NotNullCheck<T> operator!=(std::nullptr_t /*null*/) &&
    requires(kMode != ThatMode::kIgnoreCase && NullablePointer<T>)
  {
    return NotNullAccess::Make<T>(static_cast<T&&>(value_), nullptr);
  }

  template <class U>
    requires(kMode != ThatMode::kIgnoreCase && Orderable<Compared, U> &&
             Renderable<Compared> && Renderable<PeerOf<Compared, U>>)
  constexpr OperandComparison<T> operator<(const U& other) && {
    RequireOrdering<U>();
    return std::move(*this).Keep(Lt(Operand(), Peer(other), "lhs < rhs"));
  }

  template <class U>
    requires(kMode != ThatMode::kIgnoreCase && Orderable<Compared, U> &&
             Renderable<Compared> && Renderable<PeerOf<Compared, U>>)
  constexpr OperandComparison<T> operator<=(const U& other) && {
    RequireOrdering<U>();
    return std::move(*this).Keep(Le(Operand(), Peer(other), "lhs <= rhs"));
  }

  template <class U>
    requires(kMode != ThatMode::kIgnoreCase && Orderable<Compared, U> &&
             Renderable<Compared> && Renderable<PeerOf<Compared, U>>)
  constexpr OperandComparison<T> operator>(const U& other) && {
    RequireOrdering<U>();
    return std::move(*this).Keep(Gt(Operand(), Peer(other), "lhs > rhs"));
  }

  template <class U>
    requires(kMode != ThatMode::kIgnoreCase && Orderable<Compared, U> &&
             Renderable<Compared> && Renderable<PeerOf<Compared, U>>)
  constexpr OperandComparison<T> operator>=(const U& other) && {
    RequireOrdering<U>();
    return std::move(*this).Keep(Ge(Operand(), Peer(other), "lhs >= rhs"));
  }

  // Holds if the operand lies within margin of target like CHECK_NEAR(). Unlike
  // the macro, which checks both bounds separately, each operand is evaluated
  // once.
  template <class U, class M>
    requires(kMode == ThatMode::kValue && NearComparable<T, U, M> &&
             Renderable<T> && Renderable<U> && Renderable<M>)
  constexpr OperandComparison<T> Near(const U& target, const M& margin) && {
    RequireOrdering<U>();
    constexpr const char* text = "lhs near rhs";
    bool holds = false;
    if constexpr (IntegerOperand<T> && IntegerOperand<U> && IntegerOperand<M>) {
      holds = IntegersNear(value_, target, margin);
    } else if constexpr (std::is_arithmetic_v<std::remove_cvref_t<T>> &&
                         std::is_arithmetic_v<U> && std::is_arithmetic_v<M>) {
      // Computes the bounds in the common floating-point type, since the
      // integer types of the target and the margin could wrap around or
      // overflow.
      using Common = std::common_type_t<std::remove_cvref_t<T>, U, M>;
      const auto common_value = static_cast<Common>(value_);
      const auto common_target = static_cast<Common>(target);
      const auto common_margin = static_cast<Common>(margin);
      holds = common_value <= common_target + common_margin &&
              common_value >= common_target - common_margin;
    } else {
      holds = value_ <= target + margin && value_ >= target - margin;
    }
    return std::move(*this).Keep(ComparisonFactory::MakeDescribed(
        holds, text, [this, text, &target, &margin] {
          return std::format("{} ({} vs. {} +/- {})", text,
                             ComparisonFactory::Render(value_),
                             ComparisonFactory::Render(target),
                             ComparisonFactory::Render(margin));
        }));
  }

  // Holds if the operand equals target within the margin of CHECK_DOUBLE_EQ().
  template <class U>
    requires(kMode == ThatMode::kValue && NearComparable<T, U, long double>)
  constexpr OperandComparison<T> Near(const U& target) && {
    return std::move(*this).Near(target, kDoubleEqualMargin);
  }

  // Holds if the operand lies within the half-open range [low, high).
  // Integers compare by value regardless of their signedness.
  template <class L, class H>
    requires(kMode != ThatMode::kIgnoreCase && Orderable<L, Compared> &&
             Orderable<Compared, H> && Renderable<Compared> &&
             Renderable<PeerOf<Compared, L>> && Renderable<PeerOf<Compared, H>>)
  constexpr OperandComparison<T> InRange(const L& low, const H& high) && {
    RequireOrdering<L>();
    RequireOrdering<H>();
    constexpr const char* text = "lhs in range";
    const bool holds =
        LessEqualByValue(low, Operand()) && LessByValue(Operand(), high);
    return std::move(*this).Keep(ComparisonFactory::MakeDescribed(
        holds, text, [this, text, &low, &high] {
          return std::format("{} ({} vs. [{}, {}))", text,
                             ComparisonFactory::Render(Operand()),
                             ComparisonFactory::Render(Peer(low)),
                             ComparisonFactory::Render(Peer(high)));
        }));
  }

  // Holds if the operand is a valid index of a sized range like CHECK_INDEX(),
  // that is, if it is at least 0 and less than the size of the range.
  template <std::ranges::sized_range R>
    requires(kMode == ThatMode::kValue && IntegerOperand<T>)
  constexpr OperandComparison<T> IndexOf(const R& range) && {
    return std::move(*this).Keep(WithinSize<false>(range, "lhs index of rhs"));
  }

  // Holds if the operand bounds a sized range like CHECK_BOUND(), that is, if
  // it is at least 0 and at most the size of the range.
  template <std::ranges::sized_range R>
    requires(kMode == ThatMode::kValue && IntegerOperand<T>)
  constexpr OperandComparison<T> BoundOf(const R& range) && {
    return std::move(*this).Keep(WithinSize<true>(range, "lhs bound of rhs"));
  }

  // Rejects an operand without a comparison.
  template <class U = T>
  constexpr operator Comparison() const {
    static_assert(kDependentFalse<U>,
                  "That() expects a comparison such as That(a) == b");
    return ComparisonFactory::MakeConstant(false, "");
  }

 private:
  // Moves the operand into the result of the comparison, which must be
  // evaluated before.
  constexpr OperandComparison<T> Keep(Comparison comparison) && {
    return OperandAccess::Make<T>(std::move(comparison),
                                  static_cast<T&&>(value_));
  }

  // Compares the operand by value with 0 and the size of the range, which it
  // may equal only if kIncludesSize.
  template <bool kIncludesSize, class R>
  constexpr Comparison WithinSize(const R& range, const char* text) const {
    const auto size = std::ranges::size(range);
    const bool below_size = kIncludesSize ? std::cmp_less_equal(value_, size)
                                          : std::cmp_less(value_, size);
    const bool holds = std::cmp_greater_equal(value_, 0) && below_size;
    return ComparisonFactory::MakeDescribed(holds, text, [this, text, size] {
      return std::format("{} ({} vs. size {})", text,
                         ComparisonFactory::Render(value_), size);
    });
  }

  template <bool kEqual, class U>
  constexpr Comparison Equal(const U& other) const {
    constexpr const char* text = kEqual ? "lhs == rhs" : "lhs != rhs";
    static_assert(!(kTextual && OtherCstring<T> && OtherCstring<U>),
                  "That() compares C strings of char only. Compare "
                  "std::basic_string_view operands instead, or use AsPointer() "
                  "to compare addresses.");
    if constexpr (Cstring<Compared> && Cstring<U> &&
                  kMode == ThatMode::kIgnoreCase) {
      return ComparisonFactory::Separated(kEqual
                                              ? StrCaseEq(value_, other, text)
                                              : StrCaseNe(value_, other, text));
    } else if constexpr (Cstring<Compared> && Cstring<U>) {
      return ComparisonFactory::Separated(kEqual ? StrEq(value_, other, text)
                                                 : StrNe(value_, other, text));
    } else if constexpr (kEqual) {
      return Eq(Operand(), Peer(other), text);
    } else {
      return Ne(Operand(), Peer(other), text);
    }
  }

  // The operand, a pointer compared by address converted to an untyped one and
  // an array converted to a pointer, since C++26 rejects comparing arrays.
  constexpr decltype(auto) Operand() const noexcept {
    if constexpr (kByAddress) {
      return static_cast<const void*>(value_);
    } else if constexpr (std::is_array_v<std::remove_reference_t<T>>) {
      return static_cast<std::decay_t<T>>(value_);
    } else {
      return (value_);
    }
  }

  // Converts a C string compared with an untyped pointer to one as well, so
  // that both compare and print as addresses. Arrays are converted to
  // pointers.
  template <class U>
  static constexpr decltype(auto) Peer(const U& other) noexcept {
    if constexpr (std::same_as<std::decay_t<Compared>, const void*> &&
                  AnyCstring<U>) {
      return static_cast<const void*>(other);
    } else if constexpr (std::is_array_v<U>) {
      return static_cast<std::decay_t<const U&>>(other);
    } else {
      return (other);
    }
  }

  template <class U>
  static constexpr void RequireOrdering() {
    static_assert(!(kTextual && AnyCstring<U>),
                  "That() cannot order C strings. Compare "
                  "std::basic_string_view operands instead, or use AsPointer() "
                  "to compare addresses.");
  }

  T value_;
};

}  // namespace internal

// Wraps the left operand of a comparison for Ensure(), such as That(a) == b.
// Compares like NGLOG_COMPARE(), except that C strings compare by their
// contents. A failure cannot name the operands and reads "lhs == rhs (1 vs.
// 2)". The operand may contain any operator.
template <class T>
constexpr internal::ThatOperand<T> That(T&& value) noexcept(
    std::is_nothrow_constructible_v<internal::ThatOperand<T>, T&&>) {
  return internal::ThatOperand<T>{static_cast<T&&>(value)};
}

namespace internal {

// Logs a failed check described by failure with the attributes and the
// message, or fails constant evaluation.
template <int StripLevel, bool DebugMode, class A, class F, class... Args>
constexpr void Fail(const EnsureFailure& failure, const A& attributes,
                    const F& format, Args&... args) {
  if (std::is_constant_evaluated()) {
    FailConstantEvaluation<DebugMode, A>();
    return;
  }
  LogTraits<A>::Emit(AsAttributes(attributes).Capture().Prepend(failure),
                     {StripLevel, DebugMode}, format, {}, args...);
}

// Logs a failed condition with the attributes and the message like CHECK(),
// or fails constant evaluation. A signal-safe message is written after the
// prefix, since its destination runs no Prefix() hooks.
template <int StripLevel, bool DebugMode, class A, class F, class... Args>
constexpr void FailCondition(const A& attributes, const F& format,
                             Args&... args) {
  if (std::is_constant_evaluated()) {
    FailConstantEvaluation<DebugMode, A>();
    return;
  }
  LogTraits<A>::Emit(
      AsAttributes(attributes).Capture().Prepend(EnsureFailure{}),
      {StripLevel, DebugMode}, format, kCheckFailedPrefix, args...);
}

// Fails like CHECK_NOTNULL() if the operand of the null check is null and
// returns the operand otherwise.
template <int StripLevel, bool DebugMode, class T, class A, class F,
          class... Args>
constexpr T FailIfNull(NotNullCheck<T>&& check, const A& attributes,
                       const F& format, Args&... args) {
  if (NotNullAccess::IsNull(check)) [[unlikely]] {
    // Constant evaluation cannot render the description.
    if (std::is_constant_evaluated()) {
      FailConstantEvaluation<DebugMode, A>();
    } else {
      const std::string description = NotNullAccess::Describe(check);
      Fail<StripLevel, DebugMode>(
          EnsureFailure{description, EnsureFailureKind::kComparison},
          attributes, format, args...);
    }
  }
  return NotNullAccess::Release(std::move(check));
}

// Fails like the comparison unless it holds and returns the operand otherwise.
template <int StripLevel, bool DebugMode, class T, class A, class F,
          class... Args>
constexpr T FailUnlessHolds(OperandComparison<T>&& comparison,
                            const A& attributes, const F& format,
                            Args&... args) {
  if (!comparison.holds()) [[unlikely]] {
    Fail<StripLevel, DebugMode>(ComparisonFactory::Failure(comparison),
                                attributes, format, args...);
  }
  return OperandAccess::Release(std::move(comparison));
}

}  // namespace internal

// Terminates with "Check failed: " followed by the message unless the
// condition holds. Attributes other than a severity apply as with Log().
template <int StripLevel = NGLOG_STRIP_LOG,
          bool DebugMode = NGLOG_DCHECK_IS_ON(), AttributeSet A, class... Args>
  requires internal::kEnsurable<A> && (!internal::kUsesOccurrence<Args...>) &&
           internal::SignalSafeAttributes<A> &&
           internal::SignalSafeArguments<A, Args...>
constexpr void Ensure(bool condition, const A& attributes,
                      internal::LogFormatString<A, Args...> format,
                      Args&&... args) {
  if (condition) [[likely]] {
    return;
  }
  internal::FailCondition<StripLevel, DebugMode>(attributes, format, args...);
}

// Terminates with "Check failed: " followed by the message.
template <int StripLevel = NGLOG_STRIP_LOG,
          bool DebugMode = NGLOG_DCHECK_IS_ON(), class... Args>
  requires(!internal::kUsesOccurrence<Args...>)
constexpr void Ensure(bool condition,
                      FormatString<std::type_identity_t<Args>...> format,
                      Args&&... args) {
  if (condition) [[likely]] {
    return;
  }
  internal::FailCondition<StripLevel, DebugMode>(Attributes<>{}, format,
                                                 args...);
}

// Terminates with the description of a failed comparison followed by the
// message unless the comparison holds. Attributes other than a severity apply
// as with Log().
template <int StripLevel = NGLOG_STRIP_LOG,
          bool DebugMode = NGLOG_DCHECK_IS_ON(), AttributeSet A, class... Args>
  requires internal::kEnsurable<A> && (!internal::kUsesOccurrence<Args...>) &&
           (!internal::kSignalSafe<A>)
constexpr void Ensure(const Comparison& comparison, const A& attributes,
                      FormatString<std::type_identity_t<Args>...> format,
                      Args&&... args) {
  if (comparison.holds()) [[likely]] {
    return;
  }
  internal::Fail<StripLevel, DebugMode>(
      internal::ComparisonFactory::Failure(comparison), attributes, format,
      args...);
}

// Terminates with the description of a failed comparison and the message.
template <int StripLevel = NGLOG_STRIP_LOG,
          bool DebugMode = NGLOG_DCHECK_IS_ON(), class... Args>
  requires(!internal::kUsesOccurrence<Args...>)
constexpr void Ensure(const Comparison& comparison,
                      FormatString<std::type_identity_t<Args>...> format,
                      Args&&... args) {
  if (comparison.holds()) [[likely]] {
    return;
  }
  internal::Fail<StripLevel, DebugMode>(
      internal::ComparisonFactory::Failure(comparison), Attributes<>{}, format,
      args...);
}

// Terminates with the description of a failed comparison unless the
// comparison holds. Attributes other than a severity apply as with Log().
template <int StripLevel = NGLOG_STRIP_LOG,
          bool DebugMode = NGLOG_DCHECK_IS_ON(), AttributeSet A>
  requires internal::kEnsurable<A> && (!internal::kSignalSafe<A>)
constexpr void Ensure(
    const Comparison& comparison, const A& attributes,
    std::source_location location = std::source_location::current()) {
  if (comparison.holds()) [[likely]] {
    return;
  }
  internal::Fail<StripLevel, DebugMode>(
      internal::ComparisonFactory::Failure(comparison), attributes,
      internal::EmptyFormat{location});
}

// Terminates with the description of a failed comparison.
template <int StripLevel = NGLOG_STRIP_LOG,
          bool DebugMode = NGLOG_DCHECK_IS_ON()>
constexpr void Ensure(
    const Comparison& comparison,
    std::source_location location = std::source_location::current()) {
  if (comparison.holds()) [[likely]] {
    return;
  }
  internal::Fail<StripLevel, DebugMode>(
      internal::ComparisonFactory::Failure(comparison), Attributes<>{},
      internal::EmptyFormat{location});
}

// Terminates with the description of a failed comparison created by That()
// followed by the message unless the comparison holds. Returns the operand
// otherwise. Attributes other than a severity apply as with Log().
template <int StripLevel = NGLOG_STRIP_LOG,
          bool DebugMode = NGLOG_DCHECK_IS_ON(), class T, AttributeSet A,
          class... Args>
  requires internal::kEnsurable<A> && (!internal::kUsesOccurrence<Args...>) &&
           (!internal::kSignalSafe<A>)
constexpr T Ensure(OperandComparison<T>&& comparison, const A& attributes,
                   FormatString<std::type_identity_t<Args>...> format,
                   Args&&... args) {
  return internal::FailUnlessHolds<StripLevel, DebugMode>(
      std::move(comparison), attributes, format, args...);
}

// Terminates with the description of a failed comparison created by That()
// followed by the message unless the comparison holds. Returns the operand
// otherwise.
template <int StripLevel = NGLOG_STRIP_LOG,
          bool DebugMode = NGLOG_DCHECK_IS_ON(), class T, class... Args>
  requires(!internal::kUsesOccurrence<Args...>)
constexpr T Ensure(OperandComparison<T>&& comparison,
                   FormatString<std::type_identity_t<Args>...> format,
                   Args&&... args) {
  return internal::FailUnlessHolds<StripLevel, DebugMode>(
      std::move(comparison), Attributes<>{}, format, args...);
}

// Terminates with the description of a failed comparison created by That()
// unless the comparison holds. Returns the operand otherwise. Attributes other
// than a severity apply as with Log().
template <int StripLevel = NGLOG_STRIP_LOG,
          bool DebugMode = NGLOG_DCHECK_IS_ON(), class T, AttributeSet A>
  requires internal::kEnsurable<A> && (!internal::kSignalSafe<A>)
constexpr T Ensure(
    OperandComparison<T>&& comparison, const A& attributes,
    std::source_location location = std::source_location::current()) {
  return internal::FailUnlessHolds<StripLevel, DebugMode>(
      std::move(comparison), attributes, internal::EmptyFormat{location});
}

// Terminates with the description of a failed comparison created by That()
// unless the comparison holds. Returns the operand otherwise.
template <int StripLevel = NGLOG_STRIP_LOG,
          bool DebugMode = NGLOG_DCHECK_IS_ON(), class T>
constexpr T Ensure(
    OperandComparison<T>&& comparison,
    std::source_location location = std::source_location::current()) {
  return internal::FailUnlessHolds<StripLevel, DebugMode>(
      std::move(comparison), Attributes<>{}, internal::EmptyFormat{location});
}

// Terminates like CHECK_NOTNULL() followed by the message unless the operand
// is not null. Returns the operand otherwise. Attributes other than a severity
// apply as with Log().
template <int StripLevel = NGLOG_STRIP_LOG,
          bool DebugMode = NGLOG_DCHECK_IS_ON(), class T, AttributeSet A,
          class... Args>
  requires internal::kEnsurable<A> && (!internal::kUsesOccurrence<Args...>) &&
           (!internal::kSignalSafe<A>)
constexpr T Ensure(NotNullCheck<T>&& check, const A& attributes,
                   FormatString<std::type_identity_t<Args>...> format,
                   Args&&... args) {
  return internal::FailIfNull<StripLevel, DebugMode>(
      std::move(check), attributes, format, args...);
}

// Terminates like CHECK_NOTNULL() followed by the message unless the operand
// is not null. Returns the operand otherwise.
template <int StripLevel = NGLOG_STRIP_LOG,
          bool DebugMode = NGLOG_DCHECK_IS_ON(), class T, class... Args>
  requires(!internal::kUsesOccurrence<Args...>)
constexpr T Ensure(NotNullCheck<T>&& check,
                   FormatString<std::type_identity_t<Args>...> format,
                   Args&&... args) {
  return internal::FailIfNull<StripLevel, DebugMode>(
      std::move(check), Attributes<>{}, format, args...);
}

// Terminates like CHECK_NOTNULL() unless the operand is not null. Returns the
// operand otherwise. Attributes other than a severity apply as with Log().
template <int StripLevel = NGLOG_STRIP_LOG,
          bool DebugMode = NGLOG_DCHECK_IS_ON(), class T, AttributeSet A>
  requires internal::kEnsurable<A> && (!internal::kSignalSafe<A>)
constexpr T Ensure(
    NotNullCheck<T>&& check, const A& attributes,
    std::source_location location = std::source_location::current()) {
  return internal::FailIfNull<StripLevel, DebugMode>(
      std::move(check), attributes, internal::EmptyFormat{location});
}

// Terminates like CHECK_NOTNULL() unless the operand is not null. Returns the
// operand otherwise.
template <int StripLevel = NGLOG_STRIP_LOG,
          bool DebugMode = NGLOG_DCHECK_IS_ON(), class T>
constexpr T Ensure(
    NotNullCheck<T>&& check,
    std::source_location location = std::source_location::current()) {
  return internal::FailIfNull<StripLevel, DebugMode>(
      std::move(check), Attributes<>{}, internal::EmptyFormat{location});
}

}  // namespace nglog::experimental

// Compares like the CHECK_EQ() family with the expression text captured, for
// use with Ensure(). The expression is a single comparison using ==, !=, <, <=,
// > or >=. Shifts and three-way comparisons in the left operand and operators
// binding weaker than comparisons, such as && and ?:, must be parenthesized.
#  define NGLOG_COMPARE(...)                                             \
    (::nglog::experimental::internal::ComparisonDecomposer{#__VA_ARGS__} \
         ->*__VA_ARGS__)

#endif  // defined(NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_SUPPORTED)

#endif  // NGLOG_EXPERIMENTAL_ENSURE_H
