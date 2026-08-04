// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#ifndef NGLOG_INTERNAL_UTF8_H_
#define NGLOG_INTERNAL_UTF8_H_

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "ng-log/export.h"

struct stat;

namespace nglog {
namespace internal {

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
NGLOG_NO_EXPORT bool WriteUtf8(std::FILE* output, const char* input,
                               std::size_t input_length);
NGLOG_NO_EXPORT bool WriteUtf8ToFileDescriptor(int file_descriptor,
                                               const char* input,
                                               std::size_t input_length);
#ifdef NGLOG_OS_WINDOWS
NGLOG_NO_EXPORT bool WriteSignalSafeToFileDescriptor(
    int file_descriptor, const char* input, std::size_t input_length) noexcept;
#endif
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

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_UTF8_H_
