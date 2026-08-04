// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "styled_output.h"

#include <cstring>

#include "utf8.h"

#ifdef NGLOG_OS_WINDOWS
#  include <io.h>
#else
#  include "config.h"
#  ifdef HAVE_UNISTD_H
#    include <unistd.h>
#  endif
#endif

namespace nglog {
namespace internal {

void WriteRawToStderr(const char* text) {
  const std::size_t len = std::strlen(text);
#ifdef NGLOG_OS_WINDOWS
  WriteUtf8ToFileDescriptor(fileno(stderr), text, len);
#else
  if (write(fileno(stderr), text, len) < 0) {
  }
#endif
}

}  // namespace internal
}  // namespace nglog
