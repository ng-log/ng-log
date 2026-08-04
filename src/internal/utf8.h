/* Copyright (c) 2026, The ng-log contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef NGLOG_INTERNAL_UTF8_H_
#define NGLOG_INTERNAL_UTF8_H_

#include <cstddef>
#include <string>
#include <vector>

#include "ng-log/export.h"

struct stat;

namespace nglog {
namespace internal {

NGLOG_NO_EXPORT bool Utf8ToWide(const char* input, std::size_t input_length,
                                std::wstring* output);
NGLOG_NO_EXPORT bool WideToUtf8(const wchar_t* input, std::size_t input_length,
                                std::string* output);
NGLOG_NO_EXPORT int OpenUtf8(const char* path, std::size_t path_length,
                             int flags, int mode);
NGLOG_NO_EXPORT int StatUtf8(const char* path, std::size_t path_length,
                             struct stat* result);
NGLOG_NO_EXPORT int UnlinkUtf8(const char* path, std::size_t path_length);
NGLOG_NO_EXPORT int AccessUtf8(const char* path, std::size_t path_length,
                               int mode);
NGLOG_NO_EXPORT bool GetTempPathUtf8(std::string* path);
NGLOG_NO_EXPORT bool GetWindowsDirectoryUtf8(std::string* path);
NGLOG_NO_EXPORT bool ListDirectoryUtf8(const char* path,
                                       std::size_t path_length,
                                       std::vector<std::string>* entries);

}  // namespace internal
}  // namespace nglog

#endif  // NGLOG_INTERNAL_UTF8_H_
