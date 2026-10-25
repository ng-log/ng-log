// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_UTF8_H_
#define NGLOG_INTERNAL_UTF8_H_

#include <cstdio>

#include "ng-log/export.h"
#include "ng-log/platform.h"

#ifdef NGLOG_OS_WINDOWS
#  include <string>
#  include <vector>

struct stat;  // IWYU pragma: keep
#endif        // NGLOG_OS_WINDOWS

namespace nglog {
namespace internal {

NGLOG_NO_EXPORT bool WriteUtf8(std::FILE* output, const char* input,
                               std::size_t input_length);

/**
 * Wide text whose UTF-8 encoding, as WriteWideAsUtf8() writes it, occupies
 * [begin, end) of narrow text. Writers of wide characters write the wide text
 * in place of its encoding, so that it reaches them without conversion.
 */
struct WideSpan {
  const char* begin = nullptr;
  const char* end = nullptr;
  const wchar_t* text = nullptr;
  std::size_t length = 0;
};

/**
 * Writes UTF-8 text like WriteUtf8(std::FILE*, const char*, std::size_t),
 * except that a Windows console receives the wide text @p wide covers in
 * place of its encoding.
 */
NGLOG_NO_EXPORT bool WriteUtf8(std::FILE* output, const char* input,
                               std::size_t input_length, const WideSpan& wide);

//! Maximum number of bytes passed to a Utf8WriteFunction at once.
constexpr std::size_t kMaximumUtf8ChunkLength = 1024;

using Utf8WriteFunction = bool (*)(const char* input, std::size_t input_length,
                                   void* context);

/**
 * Writes the UTF-8 encoding of wide text in chunks of at most
 * kMaximumUtf8ChunkLength bytes that end on code point boundaries.
 *
 * Code units that do not form a Unicode scalar value, such as unpaired
 * surrogates, are written as U+FFFD. Nothing is allocated, so that signal
 * handlers may call the function.
 *
 * @return @c false if @p input is null while @p input_length is not zero, if
 *   @p writer is null, or if @p writer fails, which stops writing.
 */
NGLOG_NO_EXPORT bool WriteWideAsUtf8(const wchar_t* input,
                                     std::size_t input_length,
                                     Utf8WriteFunction writer,
                                     void* context) noexcept;

#ifdef NGLOG_OS_WINDOWS
enum class WindowsPathKind { kInvalid, kNarrow, kWide };

NGLOG_NO_EXPORT bool IsAscii(const char* input, std::size_t input_length);
NGLOG_NO_EXPORT WindowsPathKind ClassifyWindowsPath(const char* path,
                                                    std::size_t path_length);
NGLOG_NO_EXPORT bool PrepareWindowsPath(const wchar_t* path,
                                        std::size_t path_length,
                                        std::wstring* prepared_path);

//! Maximum number of UTF-16 code units passed to a WideWriteFunction at once.
constexpr std::size_t kMaximumWideChunkLength = 1024;

using WideWriteFunction = bool (*)(const wchar_t* input,
                                   std::size_t input_length,
                                   std::size_t* written, void* context);

NGLOG_NO_EXPORT bool Utf8ToWide(const char* input, std::size_t input_length,
                                std::wstring* output);
NGLOG_NO_EXPORT bool WideToUtf8(const wchar_t* input, std::size_t input_length,
                                std::string* output);
NGLOG_NO_EXPORT bool WideToUtf8Buffer(const wchar_t* input,
                                      std::size_t input_length, char* output,
                                      std::size_t output_capacity,
                                      std::size_t* output_length);
NGLOG_NO_EXPORT bool WriteUtf8AsWide(const char* input,
                                     std::size_t input_length,
                                     WideWriteFunction writer, void* context);
NGLOG_NO_EXPORT bool WriteWideAsChunks(const wchar_t* input,
                                       std::size_t input_length,
                                       WideWriteFunction writer, void* context);
/**
 * Writes UTF-8 text like WriteUtf8AsWide(), except for the part that @p wide
 * covers, whose wide text is written unchanged.
 *
 * The text may be a part of the text @p wide refers to, such as a line of it.
 * A part that ends within an encoded code point is converted instead.
 */
NGLOG_NO_EXPORT bool WriteUtf8WithWideAsWide(const char* input,
                                             std::size_t input_length,
                                             const WideSpan& wide,
                                             WideWriteFunction writer,
                                             void* context);
NGLOG_NO_EXPORT bool WriteUtf8ToFileDescriptor(int file_descriptor,
                                               const char* input,
                                               std::size_t input_length);
NGLOG_NO_EXPORT bool WriteSignalSafeToFileDescriptor(
    int file_descriptor, const char* input, std::size_t input_length) noexcept;
NGLOG_NO_EXPORT int OpenUtf8(const char* path, std::size_t path_length,
                             int flags, int mode);
NGLOG_NO_EXPORT int StatUtf8(const char* path, std::size_t path_length,
                             struct stat* result);
NGLOG_NO_EXPORT int UnlinkUtf8(const char* path, std::size_t path_length);
NGLOG_NO_EXPORT int AccessUtf8(const char* path, std::size_t path_length,
                               int mode);
NGLOG_NO_EXPORT bool GetTempPathUtf8(std::string* path);
NGLOG_NO_EXPORT bool GetWindowsDirectoryUtf8(std::string* path);
NGLOG_NO_EXPORT bool GetComputerNameUtf8(std::string* name);
using DirectoryEntryCallback = bool (*)(const char* entry, std::size_t length,
                                        void* context);
NGLOG_NO_EXPORT bool ForEachDirectoryEntryUtf8(const char* path,
                                               std::size_t path_length,
                                               DirectoryEntryCallback callback,
                                               void* context);
NGLOG_NO_EXPORT bool ListDirectoryUtf8(const char* path,
                                       std::size_t path_length,
                                       std::vector<std::string>* entries);
#endif  // NGLOG_OS_WINDOWS

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_UTF8_H_
