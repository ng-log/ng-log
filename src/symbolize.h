// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Satoru Takabayashi
//
// This library provides Symbolize() function that symbolizes program
// counters to their corresponding symbol names on linux platforms.
// This library has a minimal implementation of an ELF symbol table
// reader (i.e. it doesn't depend on libelf, etc.).
//
// The algorithm used in Symbolize() is as follows.
//
//   1. Go through a list of maps in /proc/self/maps and find the map
//   containing the program counter.
//
//   2. Open the mapped file and find a regular symbol table inside.
//   Iterate over symbols in the symbol table and look for the symbol
//   containing the program counter.  If such a symbol is found,
//   obtain the symbol name, and demangle the symbol if possible.
//   If the symbol isn't found in the regular symbol table (binary is
//   stripped), try the same thing with a dynamic symbol table.
//
// Note that Symbolize() is originally implemented to be used in
// FailureSignalHandler() in base/google.cc.  Hence it doesn't use
// malloc() and other unsafe operations.  It should be both
// thread-safe and async-signal-safe.

#ifndef NGLOG_INTERNAL_SYMBOLIZE_H
#define NGLOG_INTERNAL_SYMBOLIZE_H

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "config.h"
#include "ng-log/platform.h"

#if defined(HAVE_LINK_H)
#  include <link.h>  // For ElfW() macro.
#elif defined(HAVE_ELF_H)
#  include <elf.h>
#elif defined(HAVE_SYS_EXEC_ELF_H)
#  include <sys/exec_elf.h>
#endif

#include "ng-log/export.h"

#ifdef HAVE_SYMBOLIZE

#  if !defined(SIZEOF_VOID_P) && defined(__SIZEOF_POINTER__)
#    define SIZEOF_VOID_P __SIZEOF_POINTER__
#  endif

#  if defined(HAVE_ELF_H) || defined(HAVE_SYS_EXEC_ELF_H)

// If there is no ElfW macro, let's define it by ourself.
#    ifndef ElfW
#      if SIZEOF_VOID_P == 4
#        define ElfW(type) Elf32_##type
#      elif SIZEOF_VOID_P == 8
#        define ElfW(type) Elf64_##type
#      else
#        error "Unknown sizeof(void *)"
#      endif
#    endif

namespace nglog {
inline namespace tools {

// Gets the section header for the given name, if it exists. Returns true on
// success. Otherwise, returns false.
NGLOG_NO_EXPORT
bool GetSectionHeaderByName(int fd, const char* name, size_t name_len,
                            ElfW(Shdr) * out);

}  // namespace tools
}  // namespace nglog

#  endif

namespace nglog {
inline namespace tools {

// Restrictions on the callbacks that follow:
//  - The callbacks must not use heaps but only use stacks.
//  - The callbacks must be async-signal-safe.

// Installs a callback function, which will be called right before a symbol name
// is printed. The callback is intended to be used for showing a file name and a
// line number preceding a symbol name.
// "fd" is a file descriptor of the object file containing the program
// counter "pc". The callback function should write output to "out"
// and return the size of the output written. On error, the callback
// function should return -1.
using SymbolizeCallback = int (*)(int, void*, char*, size_t, uint64_t);
NGLOG_NO_EXPORT
void InstallSymbolizeCallback(SymbolizeCallback callback);

// Installs a callback function, which will be called instead of
// OpenObjectFileContainingPcAndGetStartAddress.  The callback is expected
// to searches for the object file (from /proc/self/maps) that contains
// the specified pc.  If found, sets |start_address| to the start address
// of where this object file is mapped in memory, sets the module base
// address into |base_address|, copies the object file name into
// |out_file_name|, and attempts to open the object file.  If the object
// file is opened successfully, returns the file descriptor.  Otherwise,
// returns -1.  |out_file_name_size| is the size of the file name buffer
// (including the null-terminator).
using SymbolizeOpenObjectFileCallback = int (*)(uint64_t, uint64_t&, uint64_t&,
                                                char*, size_t);
NGLOG_NO_EXPORT
void InstallSymbolizeOpenObjectFileCallback(
    SymbolizeOpenObjectFileCallback callback);

}  // namespace tools
}  // namespace nglog

#endif

namespace nglog {
inline namespace tools {

#if defined(HAVE_SYMBOLIZE)

enum class SymbolizeOptions {
  // No additional options.
  kNone = 0,
  // Do not display source and line numbers in the symbolized output.
  kNoLineNumbers = 1
};

constexpr SymbolizeOptions operator&(SymbolizeOptions lhs,
                                     SymbolizeOptions rhs) noexcept {
  return static_cast<SymbolizeOptions>(
      static_cast<std::underlying_type_t<SymbolizeOptions>>(lhs) &
      static_cast<std::underlying_type_t<SymbolizeOptions>>(rhs));
}

constexpr SymbolizeOptions operator|(SymbolizeOptions lhs,
                                     SymbolizeOptions rhs) noexcept {
  return static_cast<SymbolizeOptions>(
      static_cast<std::underlying_type_t<SymbolizeOptions>>(lhs) |
      static_cast<std::underlying_type_t<SymbolizeOptions>>(rhs));
}

// Describes where, within the "out" buffer passed to Symbolize(), the
// "file:line" text ends up, if one was written. This lets a caller render
// that span differently from the surrounding symbol text (e.g. to color it)
// without needing to know which backend (addr2line, libbacktrace, dladdr,
// DbgHelp, ...) produced it: every backend reports the span the same way,
// regardless of whether its own output places it before or after the
// function name. "length == 0" means no such span exists, e.g. because line
// number resolution is unavailable or disabled.
struct SymbolizedFrame {
  size_t file_line_offset = 0;
  size_t file_line_length = 0;
};

// Symbolizes a program counter.  On success, returns true and write the
// symbol name to "out".  The symbol name is demangled if possible
// (supports symbols generated by GCC 3.x or newer).  Otherwise,
// returns false.  If "frame" is non-null, it is filled in with the
// location of the "file:line" text within "out" (see SymbolizedFrame).
NGLOG_NO_EXPORT bool Symbolize(
    void* pc, char* out, size_t out_size,
    SymbolizeOptions options = SymbolizeOptions::kNone,
    SymbolizedFrame* frame = nullptr);

#endif  // defined(HAVE_SYMBOLIZE)

}  // namespace tools
}  // namespace nglog

#endif  // NGLOG_INTERNAL_SYMBOLIZE_H
