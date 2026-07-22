// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//
// Resolves file names and line numbers for symbolized program counters by
// shelling out to the "addr2line" utility, and installs itself as a symbolize.h
// SymbolizeCallback so that this information is printed right before the
// demangled symbol name.
//
// The addr2line executable is entirely optional: it is looked up on PATH by the
// same mechanism the C library already uses to run any other command, and this
// module quietly does nothing if it cannot be found there.

#ifndef NGLOG_INTERNAL_ADDR2LINE_H
#define NGLOG_INTERNAL_ADDR2LINE_H

#include <cstddef>
#include <cstdint>

#include "config.h"
#include "ng-log/export.h"
#include "symbolize.h"

#ifdef HAVE_ADDR2LINE

namespace nglog {
inline namespace tools {

// Parses a single line of "addr2line" output (a "file:line" or
// "file:line:discriminator" pair, not necessarily NUL-terminated, as found
// in |input| of length |input_len|). On success, writes "file:line " to
// |out| and returns the number of bytes written. Returns 0 (and leaves
// |out| untouched) if |input| carries no usable location (e.g.
// addr2line's "??:0" placeholder for an unresolved address) or if the
// formatted result does not fit within |out_size|.
NGLOG_NO_EXPORT
std::size_t FormatAddr2LineOutput(const char* input, std::size_t input_len,
                                  char* out, std::size_t out_size);

// Installs a SymbolizeCallback (see symbolize.h) that resolves file names
// and line numbers via addr2line. Returns true once the callback has been
// installed. Does not itself check whether addr2line is available: that
// happens on every symbolization request, when addr2line is actually run.
NGLOG_NO_EXPORT
bool InstallAddr2LineSymbolizeCallback();

// Points to the function-name and "file:line" fields split out of an
// "addr2line --functions" two-line output by
// SplitAddr2LineFunctionsOutput().
struct Addr2LineFunctionsResult {
  const char* name;
  std::size_t name_len;
  const char* file_line;
  std::size_t file_line_len;
};

// Splits a two-line "addr2line --functions" output (name, then a
// "file:line" pair as accepted by FormatAddr2LineOutput()) found in
// |input| of length |input_len|. Returns false if there is no second
// line, or the name is "??" (unresolved). An unresolved second line is
// not itself a failure.
NGLOG_NO_EXPORT
bool SplitAddr2LineFunctionsOutput(const char* input, std::size_t input_len,
                                   Addr2LineFunctionsResult* result);

// Resolves the function name, and file:line if available, for |pc|
// within the object at |object_path| loaded at base address
// |relocation|, via "addr2line --functions". Writes "<file>:<line>
// <name>" (or just "<name>") to |out|. Returns false only if the name
// itself could not be resolved. Fills |frame| as documented in
// symbolize.h when non-null. Used by the Windows backend, which has no
// other way to name a symbol in a binary built with DWARF debug info.
NGLOG_NO_EXPORT
bool ResolveFunctionAndLine(const char* object_path, void* pc,
                            uint64_t relocation, char* out,
                            std::size_t out_size, SymbolizeOptions options,
                            SymbolizedFrame* frame);

}  // namespace tools
}  // namespace nglog

#endif  // HAVE_ADDR2LINE

#endif  // NGLOG_INTERNAL_ADDR2LINE_H
