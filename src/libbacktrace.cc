// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "libbacktrace.h"

#ifdef HAVE_LIBBACKTRACE

#  include <backtrace.h>

#  include <cstdint>
#  include <cstdio>
#  include <mutex>
#  include <string>

#  include "ng-log/flags.h"
#  include "symbolize.h"

namespace nglog {
inline namespace tools {

namespace {

// A missing symbol table or missing debug info is a routine, expected
// condition (e.g. a stripped binary, or a module compiled without -g), not
// something worth logging on every symbolization attempt. Only reached via
// libbacktrace's own error path, not the ordinary "nothing found" one, so
// nothing here exercises it: the test binaries always carry a symbol table.
// LCOV_EXCL_START
void ErrorCallback(void* /*data*/, const char* /*msg*/, int /*errnum*/) {}
// LCOV_EXCL_STOP

// GetBacktraceState() must always return the same state: libbacktrace has
// no documented way to merge or discard one, so creating more than one per
// process would just mean redundant ELF/DWARF parsing work. std::call_once
// makes that single-creation guarantee explicit, rather than relying on a
// function-local static's implicit (and, per frame, re-checked) guard.
std::once_flag g_backtrace_state_once;
backtrace_state* g_backtrace_state = nullptr;

}  // namespace

backtrace_state* GetBacktraceState() {
  std::call_once(g_backtrace_state_once, [] {
    // A nullptr filename lets libbacktrace locate the running executable
    // itself (e.g. via /proc/self/exe on Linux). Other loaded modules are
    // discovered on demand as addresses are looked up. threaded=1 because
    // Symbolize() may run concurrently from multiple threads (e.g. from
    // signal handlers on different threads).
    g_backtrace_state = backtrace_create_state(nullptr, /*threaded=*/1,
                                               &ErrorCallback, nullptr);
  });
  return g_backtrace_state;
}

std::size_t FormatLibbacktraceLocation(const char* filename, int lineno,
                                       char* out, std::size_t out_size) {
  if (filename == nullptr || filename[0] == '\0' || out_size == 0) {
    return 0;
  }

  const int written = std::snprintf(out, out_size, "%s:%d ", filename, lineno);

  if (written < 0 || static_cast<std::size_t>(written) >= out_size) {
    // Truncated (or an encoding error): never return a partially written
    // result.
    out[0] = '\0';
    return 0;
  }

  return static_cast<std::size_t>(written);
}

namespace {

struct PcInfoContext {
  std::string filename;
  int lineno = 0;
  bool found = false;
};

// Records the innermost (first) location backtrace_pcinfo() reports for a
// pc, and returns non-zero so backtrace_pcinfo() stops there instead of
// continuing on to any further inlined call frames.
int PcInfoCallback(void* data, uintptr_t /*pc*/, const char* filename,
                   int lineno, const char* /*function*/) {
  auto* ctx = static_cast<PcInfoContext*>(data);
  if (filename != nullptr) {
    ctx->filename = filename;
    ctx->lineno = lineno;
    ctx->found = true;
  }
  return 1;
}

// Resolves file name and line number for "pc" via backtrace_pcinfo() and
// writes "<file>:<line> " to "out". "fd" and "relocation" are unused:
// unlike Addr2LineSymbolizeCallback(), which needs the object file's path
// and load bias to build an "addr2line --exe" command line, libbacktrace
// resolves the containing module and applies the load bias internally, from
// the raw runtime "pc".
int LibbacktraceSymbolizeCallback(int /*fd*/, void* pc, char* out,
                                  std::size_t out_size,
                                  uint64_t /*relocation*/) {
  if (!FLAGS_symbolize_line_info) {
    return 0;
  }

  backtrace_state* const state = GetBacktraceState();
  if (state == nullptr) {
    // LCOV_EXCL_START: backtrace_create_state() only fails to create a
    // state if it cannot locate this process's own executable (e.g. no
    // /proc/self/exe), which does not happen for a live process on the
    // platforms this backend supports.
    return 0;
    // LCOV_EXCL_STOP
  }

  PcInfoContext ctx;
  backtrace_pcinfo(state, reinterpret_cast<uintptr_t>(pc), &PcInfoCallback,
                   &ErrorCallback, &ctx);

  if (!ctx.found) {
    return 0;
  }

  return static_cast<int>(FormatLibbacktraceLocation(
      ctx.filename.c_str(), ctx.lineno, out, out_size));
}

}  // namespace

bool InstallLibbacktraceSymbolizeCallback() {
  InstallSymbolizeCallback(&LibbacktraceSymbolizeCallback);
  return true;
}

}  // namespace tools
}  // namespace nglog

#endif  // HAVE_LIBBACKTRACE
