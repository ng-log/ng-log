// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "styled_output.h"

#include <stdio.h>

#include <cstring>

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
  if (write(fileno(stderr), text, len) < 0) {
  }
}

}  // namespace internal
}  // namespace nglog
