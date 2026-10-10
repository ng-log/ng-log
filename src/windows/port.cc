/* SPDX-FileCopyrightText: 2008-2024 Google Inc.
 * SPDX-FileCopyrightText: 2026 The ng-log contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * ---
 * Author: Craig Silverstein
 * Copied from google-perftools and modified by Shinichiro Hamaji
 */

#ifndef _WIN32
#  error You should only be including windows/port.cc in a windows environment!
#endif

#include "port.h"

#include <ctime>
#include <memory>

#include "config.h"

namespace nglog {
inline namespace tools {

std::string FormatWindowsMessage(std::uint32_t error) {
  LPSTR message = nullptr;
  const DWORD message_length = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, static_cast<DWORD>(error), 0, reinterpret_cast<LPSTR>(&message),
      0, nullptr);
  std::unique_ptr<char, decltype(&LocalFree)> release{message, &LocalFree};
  if (message_length == 0) {
    return {};
  }
  return {message, message_length};
}

#ifndef HAVE_LOCALTIME_R
struct tm* localtime_r(const std::time_t* timep, std::tm* result) {
  localtime_s(result, timep);
  return result;
}
#endif  // not HAVE_LOCALTIME_R
#ifndef HAVE_GMTIME_R
struct tm* gmtime_r(const std::time_t* timep, std::tm* result) {
  gmtime_s(result, timep);
  return result;
}
#endif  // not HAVE_GMTIME_R

}  // namespace tools
}  // namespace nglog
