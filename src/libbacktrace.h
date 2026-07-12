// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch
//
// Resolves symbol names and file names and line numbers for symbolized program
// counters via libbacktrace, an in-process DWARF and symbol table reader.
// symbolize.cc uses GetBacktraceState() directly, as an alternative backend to
// its ELF symbol table reader, for the symbol name. This module additionally
// installs itself as a symbolize.h SymbolizeCallback so that file name and line
// number information is printed right before the demangled symbol name, the
// same way addr2line.cc does for its backend.
//
// Unlike addr2line.cc, resolution happens in-process rather than through a
// subprocess. This means the calls below are not async-signal-safe:
// libbacktrace allocates on the heap the first time it parses a given module's
// debug info or symbol table. This is a deliberate trade-off, consistent with
// the one already accepted for addr2line.cc's use of fork()/exec().

#ifndef NGLOG_INTERNAL_LIBBACKTRACE_H
#define NGLOG_INTERNAL_LIBBACKTRACE_H

#include <cstddef>

#include "config.h"
#include "ng-log/export.h"

#ifdef HAVE_LIBBACKTRACE

struct backtrace_state;

namespace nglog {
inline namespace tools {

// Returns the process-wide libbacktrace state, creating it on the first
// call. Returns nullptr if the state could not be created.
NGLOG_NO_EXPORT
backtrace_state* GetBacktraceState();

// Formats "|filename|:|lineno| " into |out|. Returns the number of bytes
// written, not including the terminating NUL. Returns 0 (and leaves |out|
// untouched) if |filename| is null or empty, or if the formatted result
// does not fit within |out_size|.
NGLOG_NO_EXPORT
std::size_t FormatLibbacktraceLocation(const char* filename, int lineno,
                                       char* out, std::size_t out_size);

// Installs a SymbolizeCallback (see symbolize.h) that resolves file names
// and line numbers via libbacktrace. Returns true once the callback has
// been installed.
NGLOG_NO_EXPORT
bool InstallLibbacktraceSymbolizeCallback();

}  // namespace tools
}  // namespace nglog

#endif  // HAVE_LIBBACKTRACE

#endif  // NGLOG_INTERNAL_LIBBACKTRACE_H
