// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "ng-log/platform.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

#include "internal/attributes.h"
#include "utilities.h"

#ifdef NGLOG_OS_WINDOWS
#  include <fcntl.h>
#  include <io.h>
#else
#  include <unistd.h>
#endif

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

constexpr bool Contains(const char* text, const char* fragment) {
  for (; *text != '\0'; ++text) {
    if (StartsWith(text, fragment)) {
      return true;
    }
  }
  return false;
}

#if defined(_MSC_VER)
static_assert(Contains(NGLOG_STRINGIFY(NGLOG_ATTRIBUTE_NOINLINE), "msvc::"),
              "MSVC noinline must use a vendor attribute");
static_assert(Contains(NGLOG_STRINGIFY(NGLOG_ATTRIBUTE_ALWAYS_INLINE),
                       "msvc::"),
              "MSVC forceinline must use a vendor attribute");
#elif defined(__has_attribute)
#  if __has_attribute(noinline)
static_assert(Contains(NGLOG_STRINGIFY(NGLOG_ATTRIBUTE_NOINLINE),
                       "gnu::noinline"),
              "noinline must be applied where the compiler supports it");
#  endif
#  if __has_attribute(always_inline)
static_assert(Contains(NGLOG_STRINGIFY(NGLOG_ATTRIBUTE_ALWAYS_INLINE),
                       "gnu::always_inline"),
              "always_inline must be applied where the compiler supports it");
#  endif
#  if __has_attribute(used)
static_assert(Contains(NGLOG_STRINGIFY(NGLOG_ATTRIBUTE_USED), "gnu::used"),
              "used must be applied where the compiler supports it");
#  endif
#endif

#if defined(__GNUG__)
static_assert(Contains(NGLOG_STRINGIFY(NGLOG_ATTRIBUTE_FORMAT(printf, 1, 2)),
                       "gnu::format(printf, 1, 2)"),
              "format must forward its arguments to the attribute");
static_assert(Contains(NGLOG_STRINGIFY(NGLOG_ATTRIBUTE_FORMAT_ARG(1)),
                       "gnu::format_arg(1)"),
              "format_arg must forward its argument to the attribute");
#endif

#undef NGLOG_STRINGIFY
#undef NGLOG_STRINGIFY_IMPL

namespace {

constexpr int kExpectedValue = 42;
constexpr int kInvalidFileDescriptor = -1;
constexpr std::size_t kEmptyWriteSize = 0;

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
  // The static assertions above verify the attributes. Using the
  // declarations only keeps them from being reported as unused.
  static_cast<void>(NoInlineFunction());
  static_cast<void>(AlwaysInlineFunction());
  static_cast<void>(kUsedValue);
  FormatFunction(FormatArgument("%d"), kExpectedValue);
}

constexpr std::size_t kPipeEndCount = 2;
using PipeEnds = std::array<int, kPipeEndCount>;

#ifdef NGLOG_OS_WINDOWS
int OpenPipe(PipeEnds& ends) {
  constexpr unsigned int kPipeBufferLength = 4096;
  return _pipe(ends.data(), kPipeBufferLength, _O_BINARY);
}

int CloseFileDescriptor(int fd) { return _close(fd); }

std::intmax_t ReadFileDescriptor(int fd, char* data, unsigned int size) {
  return _read(fd, data, size);
}
#else
int OpenPipe(PipeEnds& ends) { return pipe(ends.data()); }

int CloseFileDescriptor(int fd) { return close(fd); }

std::intmax_t ReadFileDescriptor(int fd, char* data, unsigned int size) {
  return read(fd, data, size);
}
#endif

TEST(Platform, SafeWriteWritesTheData) {
  constexpr char kMessage[] = "message";
  constexpr std::size_t kMessageSize = sizeof(kMessage) - 1;
  PipeEnds file_descriptors = {};
  ASSERT_EQ(OpenPipe(file_descriptors), 0);

  EXPECT_EQ(
      nglog::internal::SafeWrite(file_descriptors[1], kMessage, kMessageSize),
      static_cast<std::intmax_t>(kMessageSize));
  ASSERT_EQ(CloseFileDescriptor(file_descriptors[1]), 0);

  // Leaves room for one more byte than written to detect excess output.
  std::array<char, kMessageSize + 1> output = {};
  const std::intmax_t bytes_read =
      ReadFileDescriptor(file_descriptors[0], output.data(),
                         static_cast<unsigned int>(output.size()));
  EXPECT_EQ(CloseFileDescriptor(file_descriptors[0]), 0);
  ASSERT_GE(bytes_read, 0);
  EXPECT_EQ(std::string(output.data(), static_cast<std::size_t>(bytes_read)),
            kMessage);
}

}  // namespace
