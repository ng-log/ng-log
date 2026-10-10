// SPDX-FileCopyrightText: 2023 Google LLC
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: Apache-2.0

#include <cstring>

#include "demangle.h"

extern "C" int LLVMFuzzerTestOneInput(const unsigned char* Data,
                                      unsigned Size) {
  if (Size >= 4095) {
    return 0;
  }
  char Buffer[Size + 1];
  std::memcpy(Buffer, Data, Size);
  Buffer[Size] = 0;
  char demangled[4096];
  nglog::Demangle(Buffer, demangled, Size);
  return 0;
}
