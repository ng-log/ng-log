// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

#include "addr2line.h"

#ifdef HAVE_ADDR2LINE

#  include <algorithm>
#  include <cctype>
#  include <chrono>
#  include <cstdint>
#  include <cstdio>
#  include <cstdlib>
#  include <cstring>
#  include <iterator>

#  include "demangle.h"
#  include "ng-log/flags.h"
#  include "subprocess.h"
#  include "symbolize.h"
#  include "utilities.h"

namespace nglog {
inline namespace tools {

namespace {

// Largest chunk of "file:line" text we accept from addr2line.
constexpr std::size_t kMaxAddr2LineOutput = 4096;

// Largest object file path this module will pass to addr2line, matching
// the common PATH_MAX on Linux. Longer paths are truncated.
constexpr std::size_t kMaxObjectPathLength = 4096;

}  // namespace

std::size_t FormatAddr2LineOutput(const char* input, std::size_t input_len,
                                  char* out, std::size_t out_size) {
  // addr2line prints exactly one "file:line" pair per requested address.
  // Only look at the first line of output. If there is no newline,
  // std::find() returns |input| + |input_len|, so |line_len| ends up as
  // |input_len|.
  const char* const newline = std::find(input, input + input_len, '\n');
  const std::size_t line_len = static_cast<std::size_t>(newline - input);

  if (line_len == 0) {
    return 0;
  }

  // The line-number field never contains a colon, so the last ':' in the
  // line separates it from the file field. Search from the end.
  const auto rend = std::make_reverse_iterator(input);
  const auto rstart = std::make_reverse_iterator(input + line_len);
  const auto rit = std::find(rstart, rend, ':');

  if (rit == rend) {
    return 0;
  }

  const char* const colon = &*rit;

  const std::size_t file_len = static_cast<std::size_t>(colon - input);
  const char* line_field = colon + 1;
  const std::size_t line_field_span_len = line_len - file_len - 1;

  if (file_len == 0 || line_field_span_len == 0) {
    return 0;
  }

  // GNU addr2line appends a " (discriminator N)" annotation after the line
  // number for addresses associated with multiple DWARF line-table
  // discriminators, e.g. multiple basic blocks mapping to the same source
  // line, which commonly happens right at a CHECK/LOG(FATAL) call site. The
  // line number itself is always the leading run of digits. Anything after
  // that (the annotation, if present) is ignored.
  const char* const digits_end = std::find_if(
      line_field, line_field + line_field_span_len,
      [](char c) { return std::isdigit(static_cast<unsigned char>(c)) == 0; });
  const std::size_t line_field_len =
      static_cast<std::size_t>(digits_end - line_field);

  if (line_field_len == 0) {
    return 0;
  }

  // "??" is addr2line's placeholder for "no debug information found".
  if (file_len == 2 && input[0] == '?' && input[1] == '?') {
    return 0;
  }

  // Checked incrementally against the shrinking |remaining| budget, rather
  // than by summing file_len and line_field_len into a single upfront
  // total, so that a sum large enough to overflow std::size_t can never
  // bypass this check.
  if (out_size == 0) {
    return 0;
  }

  std::size_t remaining = out_size - 1;     // Reserve room for the NUL.
  constexpr std::size_t kLiteralBytes = 2;  // "<file>" ':' "<line>" ' '.

  if (remaining < kLiteralBytes) {
    return 0;
  }

  remaining -= kLiteralBytes;

  if (file_len > remaining) {
    return 0;
  }

  remaining -= file_len;

  if (line_field_len > remaining) {
    return 0;
  }

  char* cursor = std::copy_n(input, file_len, out);
  *cursor++ = ':';
  cursor = std::copy_n(line_field, line_field_len, cursor);
  *cursor++ = ' ';
  *cursor = '\0';

  // Derived from the actual cursor position, rather than a separately
  // maintained formula, so the return value can never drift out of sync
  // with what was actually written.
  return static_cast<std::size_t>(cursor - out);
}

namespace {

// Runs addr2line with |argv| and copies at most |out_size| bytes of its
// stdout into |out|. Returns the number of bytes copied, or 0 on any
// failure, including a timeout. Subprocess::Wait() below forcibly
// terminates a hung addr2line rather than blocking the crash-reporting
// path forever.
std::size_t RunAddr2Line(char* const argv[], char* out, std::size_t out_size) {
  // Deliberately not the parent's environment: addr2line has no need for
  // it, and this keeps e.g. DEBUGINFOD_URLS from triggering a network
  // fetch while handling a crash.
  char* envp[] = {nullptr};

  Subprocess process;

  if (!process.Spawn(argv, envp)) {
    return 0;
  }

  process.CloseStdin();

  const std::chrono::milliseconds timeout{FLAGS_addr2line_timeout_ms};
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::size_t total_read = 0;

  while (total_read < out_size) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      break;
    }

    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    if (remaining <= std::chrono::milliseconds::zero()) {
      break;
    }

    const std::size_t bytes_read =
        process.ReadStdout(out + total_read, out_size - total_read, remaining);

    if (bytes_read == 0) {
      break;  // EOF, error, or addr2line took too long to answer.
    }

    total_read += bytes_read;
  }

  const auto now = std::chrono::steady_clock::now();
  std::chrono::milliseconds remaining = std::chrono::milliseconds::zero();
  if (now < deadline) {
    remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
  }
  process.Wait(remaining);
  return total_read;
}

int Addr2LineSymbolizeCallback(int /*fd*/, void* pc, char* out,
                               std::size_t out_size, uint64_t relocation) {
  if (!FLAGS_symbolize_line_info) {
    return 0;
  }

  // The object file's path was already written to out+1 by
  // OpenObjectFileContainingPcAndGetStartAddress() before this callback
  // runs. Copy it to a buffer of our own before overwriting out with our
  // own output below. A fixed-size stack buffer, rather than std::string,
  // keeps this callback (invoked through a plain C function pointer from
  // symbolize.cc, on what can be the crash-reporting path) free of
  // allocations that could throw.
  if (out_size < 2 || out[1] == '\0') {
    return 0;
  }

  char object_path[kMaxObjectPathLength];
  std::snprintf(object_path, sizeof(object_path), "%s", out + 1);

  const auto pc0 = reinterpret_cast<uintptr_t>(pc);
  char address_buf[2 + 2 * sizeof(uintptr_t) + 1];  // "0x" + hex digits.
  std::snprintf(address_buf, sizeof(address_buf), "0x%llx",
                static_cast<unsigned long long>(pc0 - relocation));

  // Subprocess::Spawn() requires a non-const char* const argv[], even
  // though it does not modify the strings, so these are plain mutable
  // arrays rather than const_cast targets. "--exe" and |object_path| are
  // separate argv entries rather than a single "--exe=<path>" argument to
  // avoid having to concatenate them into one string: getopt_long(), which
  // addr2line uses to parse its arguments, accepts both forms for a long
  // option with a required argument.
  char argv0[] = "addr2line";
  char exe_flag[] = "--exe";
  char* argv[] = {argv0, exe_flag, object_path, address_buf, nullptr};

  char addr2line_output[kMaxAddr2LineOutput];
  const std::size_t addr2line_output_len =
      RunAddr2Line(argv, addr2line_output, sizeof(addr2line_output));

  if (addr2line_output_len == 0) {
    return 0;
  }

  return static_cast<int>(FormatAddr2LineOutput(
      addr2line_output, addr2line_output_len, out, out_size));
}

// Mirrors symbolize.cc's own (unexported) DemangleInplace().
ATTRIBUTE_NOINLINE
void DemangleInplace(char* out, std::size_t out_size) {
  char demangled[256];  // Big enough for sane demangled symbols.

  if (!Demangle(out, demangled, sizeof(demangled))) {
    return;
  }

  const void* const terminator =
      std::memchr(demangled, '\0', sizeof(demangled));

  if (terminator == nullptr) {
    std::abort();  // Demangle() broke its NUL-termination contract.
  }

  const std::size_t len = static_cast<std::size_t>(
      static_cast<const char*>(terminator) - demangled);

  if (len + 1 <= out_size) {  // +1 for '\0'.
    std::memmove(out, demangled, len + 1);
  }
}

}  // namespace

bool SplitAddr2LineFunctionsOutput(const char* input, std::size_t input_len,
                                   Addr2LineFunctionsResult* result) {
  const char* const first_newline = std::find(input, input + input_len, '\n');

  if (first_newline == input + input_len) {
    return false;  // No second line.
  }

  std::size_t name_len = static_cast<std::size_t>(first_newline - input);

  // addr2line's stdout is opened in text mode on Windows, so its CRT
  // writes CRLF line endings. Trim a trailing '\r' before treating this
  // as the end of the function-name field, otherwise the "??" check
  // below never matches and the placeholder leaks through as a bogus
  // 3-byte resolved name.
  if (name_len > 0 && input[name_len - 1] == '\r') {
    --name_len;
  }

  if (name_len == 0) {
    return false;
  }

  // "??" is addr2line's placeholder for "no symbol found".
  if (name_len == 2 && input[0] == '?' && input[1] == '?') {
    return false;
  }

  const char* const file_line_start = first_newline + 1;
  const char* const second_newline =
      std::find(file_line_start, input + input_len, '\n');

  result->name = input;
  result->name_len = name_len;
  result->file_line = file_line_start;
  result->file_line_len =
      static_cast<std::size_t>(second_newline - file_line_start);
  return true;
}

bool ResolveFunctionAndLine(const char* object_path, void* pc,
                            uint64_t relocation, char* out,
                            std::size_t out_size, SymbolizeOptions options,
                            SymbolizedFrame* frame) {
  char object_path_buf[kMaxObjectPathLength];
  std::snprintf(object_path_buf, sizeof(object_path_buf), "%s", object_path);

  const auto pc0 = reinterpret_cast<uintptr_t>(pc);
  char address_buf[2 + 2 * sizeof(uintptr_t) + 1];  // "0x" + hex digits.
  std::snprintf(address_buf, sizeof(address_buf), "0x%llx",
                static_cast<unsigned long long>(pc0 - relocation));

  char argv0[] = "addr2line";
  char functions_flag[] = "--functions";
  char exe_flag[] = "--exe";
  char* argv[] = {argv0,           functions_flag, exe_flag,
                  object_path_buf, address_buf,    nullptr};

  char addr2line_output[kMaxAddr2LineOutput];
  const std::size_t addr2line_output_len =
      RunAddr2Line(argv, addr2line_output, sizeof(addr2line_output));

  if (addr2line_output_len == 0) {
    return false;
  }

  Addr2LineFunctionsResult result;

  if (!SplitAddr2LineFunctionsOutput(addr2line_output, addr2line_output_len,
                                     &result)) {
    return false;
  }

  char* cursor = out;
  std::size_t remaining = out_size;
  // The flag and the per-call option both gate file/line information, not
  // symbol names, so the name below is always resolved regardless of their
  // values.
  const std::size_t prefix_len =
      FLAGS_symbolize_line_info &&
              (options & SymbolizeOptions::kNoLineNumbers) ==
                  SymbolizeOptions::kNone
          ? FormatAddr2LineOutput(result.file_line, result.file_line_len,
                                  cursor, remaining)
          : 0;

  if (prefix_len > 0) {
    if (frame != nullptr) {
      frame->file_line_offset = 0;
      frame->file_line_length = prefix_len > 1 ? prefix_len - 1 : 0;
    }
    cursor += prefix_len;
    remaining -= prefix_len;
  }

  if (result.name_len >= remaining) {
    return prefix_len > 0;  // Prefix alone still stands.
  }

  std::memcpy(cursor, result.name, result.name_len);
  cursor[result.name_len] = '\0';
  DemangleInplace(cursor, remaining);
  return true;
}

bool InstallAddr2LineSymbolizeCallback() {
  InstallSymbolizeCallback(&Addr2LineSymbolizeCallback);
  return true;
}

}  // namespace tools
}  // namespace nglog

#endif  // HAVE_ADDR2LINE
