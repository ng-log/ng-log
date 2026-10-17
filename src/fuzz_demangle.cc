// SPDX-FileCopyrightText: 2023 Google LLC
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string>

#include "demangle.h"

namespace {

// The symbolizer demangles into a buffer of this size. One byte selects every
// output size up to it.
constexpr std::size_t kMaxDemangledLength = 256;

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size == 0) {
    return 0;
  }
  // The first byte selects the output size, so that both short outputs that
  // overflow and long ones are covered.
  char demangled[kMaxDemangledLength];
  const std::size_t output_size = 1 + (data[0] % sizeof(demangled));
  // The demangler expects a null-terminated name.
  const std::string mangled(reinterpret_cast<const char*>(data + 1), size - 1);
  nglog::Demangle(mangled.c_str(), demangled, output_size);
  return 0;
}
