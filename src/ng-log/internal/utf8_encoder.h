// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_UTF8_ENCODER_H
#define NGLOG_INTERNAL_UTF8_ENCODER_H

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace nglog {
namespace internal {
namespace utf8 {

constexpr std::uint32_t kOneByteLimit = 0x80;
constexpr std::uint32_t kTwoByteLimit = 0x800;
constexpr std::uint32_t kThreeByteLimit = 0x10000;
constexpr std::uint32_t kTwoByteMarker = 0xc0;
constexpr std::uint32_t kThreeByteMarker = 0xe0;
constexpr std::uint32_t kFourByteMarker = 0xf0;
constexpr std::uint32_t kContinuationMarker = 0x80;
constexpr std::uint32_t kContinuationMask = 0x3f;
constexpr unsigned kContinuationBits = 6;
constexpr std::uint32_t kHighSurrogateMin = 0xd800;
constexpr std::uint32_t kHighSurrogateMax = 0xdbff;
constexpr std::uint32_t kLowSurrogateMin = 0xdc00;
constexpr std::uint32_t kLowSurrogateMax = 0xdfff;
constexpr unsigned kSurrogateBits = 10;
constexpr std::uint32_t kSupplementaryBase = 0x10000;
constexpr std::uint32_t kMaximumCodePoint = 0x10ffff;
constexpr std::uint32_t kReplacementCharacter = 0xfffd;

// Whether wchar_t holds UTF-16 code units, whose surrogates pair up, instead
// of UTF-32 ones.
constexpr bool kWideCharIsUtf16 = sizeof(wchar_t) == sizeof(char16_t);

static_assert(sizeof(wchar_t) == sizeof(char16_t) ||
                  sizeof(wchar_t) == sizeof(char32_t),
              "wchar_t must hold UTF-16 or UTF-32 code units");

inline char ContinuationByte(std::uint32_t code_point, unsigned shift) {
  return static_cast<char>(kContinuationMarker |
                           ((code_point >> shift) & kContinuationMask));
}

// Writes the UTF-8 encoding of a Unicode scalar value and returns its length.
inline std::size_t EncodeCodePoint(std::uint32_t code_point,
                                   char* output) noexcept {
  if (code_point < kOneByteLimit) {
    output[0] = static_cast<char>(code_point);
    return 1;
  }
  if (code_point < kTwoByteLimit) {
    output[0] =
        static_cast<char>(kTwoByteMarker | (code_point >> kContinuationBits));
    output[1] = ContinuationByte(code_point, 0);
    return 2;
  }
  if (code_point < kThreeByteLimit) {
    output[0] = static_cast<char>(kThreeByteMarker |
                                  (code_point >> (2 * kContinuationBits)));
    output[1] = ContinuationByte(code_point, kContinuationBits);
    output[2] = ContinuationByte(code_point, 0);
    return 3;
  }
  output[0] = static_cast<char>(kFourByteMarker |
                                (code_point >> (3 * kContinuationBits)));
  output[1] = ContinuationByte(code_point, 2 * kContinuationBits);
  output[2] = ContinuationByte(code_point, kContinuationBits);
  output[3] = ContinuationByte(code_point, 0);
  return 4;
}

}  // namespace utf8

/**
 * Encodes wide text as UTF-8 one code unit at a time without allocating.
 *
 * wchar_t holds UTF-16 code units if it is 16 bits wide, such as on Windows,
 * and UTF-32 code units otherwise. Code units that do not form a Unicode
 * scalar value, such as unpaired surrogates and values beyond U+10FFFF, are
 * encoded as U+FFFD.
 */
class Utf8Encoder {
 public:
  //! Maximum number of bytes Put() and Finish() write at once.
  static constexpr std::size_t kMaximumLength = 6;

  /**
   * Writes the UTF-8 encoding of the code points that @p unit completes.
   *
   * A high surrogate completes no code point until the next code unit
   * arrives.
   *
   * @param output Receives at least kMaximumLength bytes.
   * @return Number of bytes written.
   */
  std::size_t Put(wchar_t unit, char* output) noexcept {
    const std::uint32_t value =
        static_cast<std::make_unsigned_t<wchar_t>>(unit);
    std::size_t length = 0;
    if (high_surrogate_ != 0) {
      const std::uint32_t high_surrogate = high_surrogate_;
      high_surrogate_ = 0;
      if (value >= utf8::kLowSurrogateMin && value <= utf8::kLowSurrogateMax) {
        return utf8::EncodeCodePoint(
            utf8::kSupplementaryBase +
                ((high_surrogate - utf8::kHighSurrogateMin)
                 << utf8::kSurrogateBits) +
                (value - utf8::kLowSurrogateMin),
            output);
      }
      length = utf8::EncodeCodePoint(utf8::kReplacementCharacter, output);
    }
    if (utf8::kWideCharIsUtf16 && value >= utf8::kHighSurrogateMin &&
        value <= utf8::kHighSurrogateMax) {
      high_surrogate_ = value;
      return length;
    }
    const bool is_scalar_value =
        (value < utf8::kHighSurrogateMin || value > utf8::kLowSurrogateMax) &&
        value <= utf8::kMaximumCodePoint;
    return length + utf8::EncodeCodePoint(
                        is_scalar_value ? value : utf8::kReplacementCharacter,
                        output + length);
  }

  /**
   * Writes U+FFFD for a high surrogate that no code unit followed.
   *
   * @param output Receives at least kMaximumLength bytes.
   * @return Number of bytes written.
   */
  std::size_t Finish(char* output) noexcept {
    if (high_surrogate_ == 0) {
      return 0;
    }
    high_surrogate_ = 0;
    return utf8::EncodeCodePoint(utf8::kReplacementCharacter, output);
  }

 private:
  // High surrogate awaiting its low surrogate, or zero.
  std::uint32_t high_surrogate_ = 0;
};

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_UTF8_ENCODER_H
