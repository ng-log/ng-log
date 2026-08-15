// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "ng-log/platform.h"

#include <cstdint>
#include <cstdio>
#include <type_traits>

#include "gtest/gtest.h"
#include "internal/attributes.h"
#include "utilities.h"

#if defined(__GNUG__) && !defined(__has_attribute) && \
    defined(NGLOG_ATTRIBUTE_ALWAYS_INLINE)
#  error "always_inline requires __has_attribute detection"
#endif

#define NGLOG_STRINGIFY_IMPL(value) #value
#define NGLOG_STRINGIFY(value) NGLOG_STRINGIFY_IMPL(value)

constexpr bool StartsWith(const char* text, const char* prefix) {
  return *prefix == '\0' || (*text != '\0' && *text == *prefix &&
                             StartsWith(text + 1, prefix + 1));
}

constexpr bool ContainsMsvcAttribute(const char* text) {
  for (; *text != '\0'; ++text) {
    if (StartsWith(text, "msvc::")) {
      return true;
    }
  }
  return false;
}

#if defined(_MSC_VER)
static_assert(ContainsMsvcAttribute(NGLOG_STRINGIFY(NGLOG_ATTRIBUTE_NOINLINE)),
              "MSVC noinline must use a vendor attribute");
static_assert(
    ContainsMsvcAttribute(NGLOG_STRINGIFY(NGLOG_ATTRIBUTE_ALWAYS_INLINE)),
    "MSVC forceinline must use a vendor attribute");
#endif

#undef NGLOG_STRINGIFY
#undef NGLOG_STRINGIFY_IMPL

namespace {

constexpr int kExpectedValue = 42;
constexpr int kInvalidFileDescriptor = -1;
constexpr std::size_t kEmptyWriteSize = 0;
constexpr std::intmax_t kExpectedEmptyWrite = 0;

static_assert(
    std::is_same<decltype(nglog::internal::SafeWrite(kInvalidFileDescriptor,
                                                     nullptr, kEmptyWriteSize)),
                 std::intmax_t>::value,
    "SafeWrite must use the widest signed integer return type");
static_assert(noexcept(nglog::internal::SafeWrite(kInvalidFileDescriptor,
                                                  nullptr, kEmptyWriteSize)),
              "SafeWrite must be noexcept");

NGLOG_ATTRIBUTE_NOINLINE
int NoInlineFunction() { return kExpectedValue; }

NGLOG_ATTRIBUTE_ALWAYS_INLINE
inline int AlwaysInlineFunction() { return kExpectedValue; }

NGLOG_ATTRIBUTE_USED
constexpr int kUsedValue = kExpectedValue;

NGLOG_ATTRIBUTE_FORMAT(printf, 1, 2)
void FormatFunction(const char*, ...) {}

NGLOG_ATTRIBUTE_FORMAT_ARG(1)
const char* FormatArgument(const char* format) { return format; }

TEST(Platform, CompilerAttributesCompile) {
  EXPECT_EQ(NoInlineFunction(), kUsedValue);
  EXPECT_EQ(AlwaysInlineFunction(), kUsedValue);
  FormatFunction("%d", kExpectedValue);
  EXPECT_STREQ("%d", FormatArgument("%d"));
  EXPECT_EQ(nglog::internal::SafeWrite(fileno(stderr), "", kEmptyWriteSize),
            kExpectedEmptyWrite);
}

}  // namespace
