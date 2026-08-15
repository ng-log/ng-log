// Copyright (c) 2024, Google Inc.
// Copyright (c) 2026, The ng-log contributors
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Author: Satoru Takabayashi
// Stack-footprint reduction work done by Raksit Ashok
//
// Implementation note:
//
// We don't use heaps but only use stacks.  We want to reduce the
// stack consumption so that the symbolizer can run on small stacks.
//
// Here are some numbers collected with GCC 4.1.0 on x86:
// - sizeof(Elf32_Sym)  = 16
// - sizeof(Elf32_Shdr) = 40
// - sizeof(Elf64_Sym)  = 24
// - sizeof(Elf64_Shdr) = 64
//
// This implementation is intended to be async-signal-safe. On POSIX systems,
// the string functions used here are listed as async-signal-safe by
// POSIX.1-2008 TC2.
//
// Additional header can be specified by the NGLOG_BUILD_CONFIG_INCLUDE
// macro to add platform specific defines (e.g. NGLOG_OS_OPENBSD).

#ifdef NGLOG_BUILD_CONFIG_INCLUDE
#  include NGLOG_BUILD_CONFIG_INCLUDE
#endif  // NGLOG_BUILD_CONFIG_INCLUDE

#include "symbolize.h"

#include "utilities.h"

#if defined(HAVE_SYMBOLIZE)

#  include <algorithm>
#  include <cstdlib>
#  include <cstring>
#  include <limits>

#  include "demangle.h"

// We don't use assert() since it's not guaranteed to be
// async-signal-safe.  Instead we define a minimal assertion
// macro. So far, we don't need pretty printing for __FILE__, etc.
#  define NGLOG_SAFE_ASSERT(expr) ((expr) ? 0 : (std::abort(), 0))

namespace nglog {
inline namespace tools {

namespace {

SymbolizeCallback g_symbolize_callback = nullptr;
SymbolizeOpenObjectFileCallback g_symbolize_open_object_file_callback = nullptr;

// This function wraps the Demangle function to provide an interface
// where the input symbol is demangled in-place.
// To keep stack consumption low, we would like this function to not
// get inlined.
//
// Unused on the Windows addr2line backend below: addr2line.cc does its
// own demangling via ResolveFunctionAndLine(), so guard the definition
// itself rather than leave it to trip -Wunused-function there.
#  if !(defined(NGLOG_OS_WINDOWS) && defined(HAVE_ADDR2LINE))
NGLOG_ATTRIBUTE_NOINLINE
void DemangleInplace(char* out, size_t out_size) {
  char demangled[256];  // Big enough for sane demangled symbols.
  if (Demangle(out, demangled, sizeof(demangled))) {
    // Demangling succeeded. Copy to out if the space allows. The scan is
    // bounded (std::memchr() rather than strlen(); strnlen() is POSIX,
    // not standard C++) so that a Demangle() implementation which breaks
    // its contract (reporting success without '\0'-terminating the
    // output) trips the assertion below instead of the scan reading past
    // the buffer.
    const void* const terminator =
        std::memchr(demangled, '\0', sizeof(demangled));
    NGLOG_SAFE_ASSERT(terminator != nullptr);
    const size_t len =
        static_cast<size_t>(static_cast<const char*>(terminator) - demangled);
    if (len + 1 <= out_size) {  // +1 for '\0'.
      memmove(out, demangled, len + 1);
    }
  }
}
#  endif  // !(defined(NGLOG_OS_WINDOWS) && defined(HAVE_ADDR2LINE))

}  // namespace

void InstallSymbolizeCallback(SymbolizeCallback callback) {
  g_symbolize_callback = callback;
}

void InstallSymbolizeOpenObjectFileCallback(
    SymbolizeOpenObjectFileCallback callback) {
  g_symbolize_open_object_file_callback = callback;
}

}  // namespace tools
}  // namespace nglog

#  if defined(HAVE_LIBBACKTRACE)

#    include <backtrace.h>

#    include <cstdint>

#    include "libbacktrace.h"

namespace nglog {
inline namespace tools {

namespace {

struct SymInfoContext {
  char* out;
  size_t out_size;
  bool found;
};

void SymInfoCallback(void* data, uintptr_t /*pc*/, const char* symname,
                     uintptr_t /*symval*/, uintptr_t /*symsize*/) {
  auto* ctx = static_cast<SymInfoContext*>(data);
  if (symname == nullptr || ctx->out_size == 0) {
    return;
  }
  const size_t symbol_length = std::strlen(symname);
  if (symbol_length >= ctx->out_size) {
    return;
  }
  std::memcpy(ctx->out, symname, symbol_length + 1);
  ctx->found = true;
}

// Only reached via libbacktrace's own error path (e.g. a module with no
// symbol table at all), not the ordinary "nothing found" one, so nothing
// here exercises it: the test binaries always carry a symbol table.
// LCOV_EXCL_START
void ErrorCallback(void* /*data*/, const char* /*msg*/, int /*errnum*/) {}
// LCOV_EXCL_STOP

}  // namespace

// Uses libbacktrace's symbol table reader (backtrace_syminfo()) in place of
// the hand-rolled ELF reader below, and libbacktrace's DWARF reader
// (backtrace_pcinfo(), via the callback InstallLibbacktraceSymbolizeCallback()
// installs) in place of addr2line for file name and line number
// information. Unlike the branches below, module discovery for a given
// "pc" is handled entirely inside libbacktrace, so neither
// OpenObjectFileContainingPcAndGetStartAddress() nor
// g_symbolize_open_object_file_callback is consulted here.
NGLOG_ATTRIBUTE_NOINLINE
static bool SymbolizeAndDemangle(void* pc, char* out, size_t out_size,
                                 SymbolizeOptions options,
                                 SymbolizedFrame* frame) {
  if (out_size < 1) {
    return false;
  }
  out[0] = '\0';

  if (g_symbolize_callback && (options & SymbolizeOptions::kNoLineNumbers) !=
                                  SymbolizeOptions::kNoLineNumbers) {
    // Run the file/line callback first, exactly as the ELF reader below
    // does, so its output ends up as a prefix before the symbol name. "fd"
    // and "relocation" are unused by LibbacktraceSymbolizeCallback():
    // libbacktrace resolves the containing module and applies the load
    // bias internally, from the raw runtime "pc".
    int num_bytes_written = g_symbolize_callback(-1, pc, out, out_size, 0);
    if (num_bytes_written > 0) {
      if (frame != nullptr) {
        // The callback writes "<file>:<line> " (see
        // FormatLibbacktraceLocation()). Trim the trailing space so the
        // span covers exactly "<file>:<line>".
        const auto written = static_cast<size_t>(num_bytes_written);
        frame->file_line_offset = 0;
        frame->file_line_length = written > 1 ? written - 1 : 0;
      }
      out += static_cast<size_t>(num_bytes_written);
      out_size -= static_cast<size_t>(num_bytes_written);
    }
  }

  backtrace_state* const state = GetBacktraceState();
  if (state == nullptr) {
    return false;
  }

  SymInfoContext ctx{out, out_size, false};
  backtrace_syminfo(state, reinterpret_cast<uintptr_t>(pc), &SymInfoCallback,
                    &ErrorCallback, &ctx);

  if (!ctx.found) {
    return false;
  }

  // Symbolization succeeded.  Now we try to demangle the symbol.
  DemangleInplace(out, out_size);
  return true;
}

}  // namespace tools
}  // namespace nglog

#  elif defined(HAVE_LINK_H)

#    if defined(HAVE_DLFCN_H)
#      include <dlfcn.h>
#    endif
#    include <fcntl.h>
#    include <sys/stat.h>
#    include <sys/types.h>
#    include <unistd.h>

#    include <cerrno>
#    include <climits>
#    include <cstddef>
#    include <cstdint>
#    include <cstdio>
#    include <cstdlib>
#    include <cstring>

#    include "config.h"
#    include "internal/checked_arithmetic.h"
#    include "ng-log/raw_logging.h"
#    include "symbolize.h"

namespace nglog {
inline namespace tools {

namespace {

// Re-runs run until it doesn't cause EINTR.
// Similar to the TEMP_FAILURE_RETRY macro from GNU C.
template <class Functor>
auto FailureRetry(Functor run, int error = EINTR) noexcept(noexcept(run())) {
  decltype(run()) result;

  while ((result = run()) == -1 && errno == error) {
  }

  return result;
}

}  // namespace

// Read up to "count" bytes from "offset" in the file pointed by file
// descriptor "fd" into the buffer starting at "buf" while handling short reads
// and EINTR.  On success, return the number of bytes read.  Otherwise, return
// -1.
static ssize_t ReadFromOffset(const int fd, void* buf, const std::size_t count,
                              const std::size_t offset) {
  constexpr std::size_t kMaxReadCount =
      static_cast<std::size_t>(std::numeric_limits<ssize_t>::max());
  constexpr std::size_t kMaxFileOffset =
      static_cast<std::size_t>(std::numeric_limits<off_t>::max());
  if (fd < 0 || count > kMaxReadCount || offset > kMaxFileOffset) {
    return -1;
  }
  char* buf0 = reinterpret_cast<char*>(buf);
  std::size_t num_bytes = 0;
  while (num_bytes < count) {
    std::size_t read_offset;
    if (!internal::CheckedAdd(offset, num_bytes, read_offset) ||
        read_offset > kMaxFileOffset) {
      return -1;
    }
    ssize_t len = FailureRetry(
        [fd, p = buf0 + num_bytes, n = count - num_bytes,
         m = static_cast<off_t>(read_offset)] { return pread(fd, p, n, m); });
    if (len < 0) {  // There was an error other than EINTR.
      return -1;
    }
    if (len == 0) {  // Reached EOF.
      break;
    }
    num_bytes += static_cast<std::size_t>(len);
  }
  return static_cast<ssize_t>(num_bytes);
}

// Try reading exactly "count" bytes from "offset" bytes in a file
// pointed by "fd" into the buffer starting at "buf" while handling
// short reads and EINTR.  On success, return true. Otherwise, return
// false.
static bool ReadFromOffsetExact(const int fd, void* buf,
                                const std::size_t count,
                                const std::size_t offset) {
  ssize_t len = ReadFromOffset(fd, buf, count, offset);
  return static_cast<std::size_t>(len) == count;
}

static bool IsFileRangeReadable(const int fd, const std::uint64_t offset,
                                const std::uint64_t size) {
  struct stat file_status;
  if (fstat(fd, &file_status) != 0 || file_status.st_size < 0) {
    return false;
  }
  const std::uint64_t file_size =
      static_cast<std::uint64_t>(file_status.st_size);
  return offset <= file_size && size <= file_size - offset;
}

static bool IsSupportedElfHeader(const ElfW(Ehdr) & elf_header) {
  constexpr unsigned char kExpectedElfClass =
      sizeof(void*) == sizeof(std::uint32_t) ? ELFCLASS32 : ELFCLASS64;
  const std::uint16_t value = 1;
  const bool is_little_endian =
      *reinterpret_cast<const unsigned char*>(&value) == 1;
  const unsigned char expected_data =
      is_little_endian ? ELFDATA2LSB : ELFDATA2MSB;
  return std::memcmp(elf_header.e_ident, ELFMAG, SELFMAG) == 0 &&
         elf_header.e_ident[EI_CLASS] == kExpectedElfClass &&
         elf_header.e_ident[EI_DATA] == expected_data &&
         elf_header.e_ident[EI_VERSION] == EV_CURRENT &&
         elf_header.e_version == EV_CURRENT &&
         elf_header.e_ehsize == sizeof(ElfW(Ehdr));
}

static bool ReadSectionHeader(const int fd, const std::size_t section_index,
                              const std::size_t section_count,
                              const std::size_t section_offset,
                              const std::size_t section_entry_size,
                              ElfW(Shdr) * out) {
  if (section_index >= section_count ||
      section_entry_size < sizeof(ElfW(Shdr))) {
    return false;
  }
  std::size_t section_table_offset;
  std::size_t section_header_offset;
  if (!internal::CheckedMultiply(section_index, section_entry_size,
                                 section_table_offset) ||
      !internal::CheckedAdd(section_offset, section_table_offset,
                            section_header_offset)) {
    return false;
  }
  return ReadFromOffsetExact(fd, out, sizeof(*out), section_header_offset);
}

static bool GetSectionTableInfo(const int fd, const ElfW(Ehdr) & elf_header,
                                std::size_t* section_count,
                                std::size_t* string_table_index,
                                std::size_t* section_entry_size) {
  if (elf_header.e_shoff == 0 && elf_header.e_shnum == 0) {
    *section_count = 0;
    if (string_table_index != nullptr) {
      *string_table_index = 0;
    }
    if (section_entry_size != nullptr) {
      *section_entry_size = 0;
    }
    return true;
  }
  if (elf_header.e_shentsize < sizeof(ElfW(Shdr))) {
    return false;
  }
  const std::size_t section_offset =
      static_cast<std::size_t>(elf_header.e_shoff);
  const std::size_t entry_size = elf_header.e_shentsize;

  std::size_t count = elf_header.e_shnum;
  std::size_t string_index = elf_header.e_shstrndx;
  ElfW(Shdr) first_section;
  if (elf_header.e_shnum == 0 || elf_header.e_shstrndx == SHN_XINDEX) {
    if (section_offset == 0 ||
        !ReadFromOffsetExact(fd, &first_section, sizeof(first_section),
                             section_offset)) {
      return false;
    }
    if (elf_header.e_shnum == 0) {
      count = static_cast<std::size_t>(first_section.sh_size);
    }
    if (elf_header.e_shstrndx == SHN_XINDEX) {
      string_index = first_section.sh_link;
    }
  }
  if (count == 0) {
    return false;
  }
  std::size_t section_table_size;
  if (!internal::CheckedMultiply(count, entry_size, section_table_size) ||
      !IsFileRangeReadable(fd, section_offset, section_table_size) ||
      (string_table_index != nullptr && string_index >= count)) {
    return false;
  }
  *section_count = count;
  if (string_table_index != nullptr) {
    *string_table_index = string_index;
  }
  if (section_entry_size != nullptr) {
    *section_entry_size = entry_size;
  }
  return true;
}

// Returns elf_header.e_type if the file pointed by fd is an ELF binary.
static int FileGetElfType(const int fd) {
  ElfW(Ehdr) elf_header;
  if (!ReadFromOffsetExact(fd, &elf_header, sizeof(elf_header), 0)) {
    return -1;
  }
  if (!IsSupportedElfHeader(elf_header)) {
    return -1;
  }
  return elf_header.e_type;
}

// Read the section headers in the given ELF binary, and if a section
// of the specified type is found, set the output to this section header
// and return kFound. Return kNotFound when no section matches and kMalformed
// when the section table cannot be read safely.
// To keep stack consumption low, we would like this function to not get
// inlined.
enum class SectionLookupResult { kNotFound, kFound, kMalformed };

NGLOG_ATTRIBUTE_NOINLINE
static SectionLookupResult GetSectionHeaderByType(
    const int fd, const std::size_t sh_num, const std::size_t sh_offset,
    const std::size_t sh_entry_size, ElfW(Word) type, ElfW(Shdr) * out) {
  std::size_t section_table_size;
  if (!internal::CheckedMultiply(sh_num, sh_entry_size, section_table_size) ||
      sh_entry_size < sizeof(ElfW(Shdr)) ||
      !IsFileRangeReadable(fd, sh_offset, section_table_size)) {
    return SectionLookupResult::kMalformed;
  }

  if (sh_entry_size != sizeof(ElfW(Shdr))) {
    for (std::size_t i = 0; i < sh_num; ++i) {
      ElfW(Shdr) section;
      if (!ReadSectionHeader(fd, i, sh_num, sh_offset, sh_entry_size,
                             &section)) {
        return SectionLookupResult::kMalformed;
      }
      if (section.sh_type == type) {
        *out = section;
        return SectionLookupResult::kFound;
      }
    }
    return SectionLookupResult::kNotFound;
  }

  // Read at most 16 section headers at a time to save read calls.
  ElfW(Shdr) buf[16];
  for (std::size_t i = 0; i < sh_num;) {
    std::size_t num_bytes_left;
    if (!internal::CheckedMultiply(sh_num - i, sizeof(buf[0]),
                                   num_bytes_left)) {
      return SectionLookupResult::kMalformed;
    }
    const std::size_t num_bytes_to_read =
        (sizeof(buf) > num_bytes_left) ? num_bytes_left : sizeof(buf);
    std::size_t read_offset;
    if (!internal::CheckedMultiply(i, sizeof(buf[0]), read_offset) ||
        !internal::CheckedAdd(sh_offset, read_offset, read_offset) ||
        !ReadFromOffsetExact(fd, buf, num_bytes_to_read, read_offset)) {
      return SectionLookupResult::kMalformed;
    }
    const std::size_t num_headers_in_buf = num_bytes_to_read / sizeof(buf[0]);
    for (std::size_t j = 0; j < num_headers_in_buf; ++j) {
      if (buf[j].sh_type == type) {
        *out = buf[j];
        return SectionLookupResult::kFound;
      }
    }
    i += num_headers_in_buf;
  }
  return SectionLookupResult::kNotFound;
}

// There is no particular reason to limit section name to 63 characters,
// but there has (as yet) been no need for anything longer either.
constexpr std::size_t kMaxSectionNameLen = 64;

// name_len should include terminating '\0'.
bool GetSectionHeaderByName(int fd, const char* name, std::size_t name_len,
                            ElfW(Shdr) * out) {
  if (name == nullptr || out == nullptr || name_len == 0) {
    return false;
  }
  ElfW(Ehdr) elf_header;
  if (!ReadFromOffsetExact(fd, &elf_header, sizeof(elf_header), 0)) {
    return false;
  }
  if (!IsSupportedElfHeader(elf_header) || name_len > kMaxSectionNameLen) {
    return false;
  }

  std::size_t section_count;
  std::size_t string_table_index;
  std::size_t section_entry_size;
  if (!GetSectionTableInfo(fd, elf_header, &section_count, &string_table_index,
                           &section_entry_size) ||
      section_count == 0) {
    return false;
  }
  const std::size_t section_offset =
      static_cast<std::size_t>(elf_header.e_shoff);
  ElfW(Shdr) shstrtab;
  if (!ReadSectionHeader(fd, string_table_index, section_count, section_offset,
                         section_entry_size, &shstrtab) ||
      shstrtab.sh_type != SHT_STRTAB) {
    return false;
  }
  if (!IsFileRangeReadable(fd, static_cast<std::uint64_t>(shstrtab.sh_offset),
                           static_cast<std::uint64_t>(shstrtab.sh_size))) {
    return false;
  }

  for (std::size_t i = 0; i < section_count; ++i) {
    if (!ReadSectionHeader(fd, i, section_count, section_offset,
                           section_entry_size, out)) {
      return false;
    }
    char header_name[kMaxSectionNameLen];
    if (out->sh_name > shstrtab.sh_size ||
        name_len > shstrtab.sh_size - out->sh_name) {
      continue;
    }
    std::size_t name_offset;
    if (!internal::CheckedAdd(static_cast<std::size_t>(shstrtab.sh_offset),
                              static_cast<std::size_t>(out->sh_name),
                              name_offset) ||
        !ReadFromOffsetExact(fd, header_name, name_len, name_offset)) {
      return false;
    }
    if (std::memcmp(header_name, name, name_len) == 0) {
      return true;
    }
  }
  return false;
}

enum class SymbolLookupResult { kNotFound, kFound, kMalformed };

static bool GetProgramHeaderTableInfo(const int fd,
                                      const ElfW(Ehdr) & elf_header,
                                      std::size_t* program_header_count) {
  std::size_t count = elf_header.e_phnum;
  if (elf_header.e_phnum == 0) {
    *program_header_count = 0;
    return true;
  }
  if (elf_header.e_phnum == PN_XNUM) {
    if (elf_header.e_shoff == 0 ||
        elf_header.e_shentsize < sizeof(ElfW(Shdr))) {
      return false;
    }
    ElfW(Shdr) first_section;
    if (!ReadFromOffsetExact(fd, &first_section, sizeof(first_section),
                             static_cast<std::size_t>(elf_header.e_shoff))) {
      return false;
    }
    count = static_cast<std::size_t>(first_section.sh_info);
  }
  if (count == 0) {
    *program_header_count = 0;
    return true;
  }
  if (elf_header.e_phentsize < sizeof(ElfW(Phdr))) {
    return false;
  }
  std::size_t table_size;
  if (!internal::CheckedMultiply(
          count, static_cast<std::size_t>(elf_header.e_phentsize),
          table_size) ||
      !IsFileRangeReadable(fd, static_cast<std::uint64_t>(elf_header.e_phoff),
                           table_size)) {
    return false;
  }
  *program_header_count = count;
  return true;
}

static bool ReadProgramHeader(const int fd, const std::size_t index,
                              const std::size_t count,
                              const std::size_t table_offset,
                              const std::size_t entry_size, ElfW(Phdr) * out) {
  if (index >= count || entry_size < sizeof(ElfW(Phdr))) {
    return false;
  }
  std::size_t entry_offset;
  std::size_t offset;
  if (!internal::CheckedMultiply(index, entry_size, entry_offset) ||
      !internal::CheckedAdd(table_offset, entry_offset, offset)) {
    return false;
  }
  return ReadFromOffsetExact(fd, out, sizeof(*out), offset);
}

static bool ConvertDynamicAddressToFileOffset(
    const int fd, const ElfW(Ehdr) & elf_header,
    const std::size_t program_header_count, const std::uint64_t address,
    const std::uint64_t size, std::size_t* file_offset) {
  const std::size_t table_offset = static_cast<std::size_t>(elf_header.e_phoff);
  const std::size_t entry_size = elf_header.e_phentsize;
  for (std::size_t i = 0; i < program_header_count; ++i) {
    ElfW(Phdr) program_header;
    if (!ReadProgramHeader(fd, i, program_header_count, table_offset,
                           entry_size, &program_header)) {
      return false;
    }
    const std::uint64_t segment_vaddr =
        static_cast<std::uint64_t>(program_header.p_vaddr);
    const std::uint64_t segment_filesz =
        static_cast<std::uint64_t>(program_header.p_filesz);
    if (program_header.p_type != PT_LOAD || address < segment_vaddr ||
        address - segment_vaddr > segment_filesz ||
        size > segment_filesz - (address - segment_vaddr)) {
      continue;
    }
    std::uint64_t offset;
    if (!internal::CheckedAdd(
            static_cast<std::uint64_t>(program_header.p_offset),
            address - segment_vaddr, offset) ||
        offset > std::numeric_limits<std::size_t>::max() ||
        !IsFileRangeReadable(fd, offset, size)) {
      return false;
    }
    *file_offset = static_cast<std::size_t>(offset);
    return true;
  }
  return false;
}

static bool ReadSysVHashSymbolCount(const int fd, const std::size_t offset,
                                    std::size_t* symbol_count) {
  std::uint32_t header[2];
  if (!IsFileRangeReadable(fd, offset, sizeof(header)) ||
      !ReadFromOffsetExact(fd, header, sizeof(header), offset)) {
    return false;
  }
  std::size_t word_count;
  if (!internal::CheckedAdd(static_cast<std::size_t>(2),
                            static_cast<std::size_t>(header[0]), word_count) ||
      !internal::CheckedAdd(word_count, static_cast<std::size_t>(header[1]),
                            word_count)) {
    return false;
  }
  std::size_t table_size;
  if (!internal::CheckedMultiply(word_count, sizeof(std::uint32_t),
                                 table_size) ||
      !IsFileRangeReadable(fd, offset, table_size)) {
    return false;
  }
  *symbol_count = header[1];
  return true;
}

static bool ReadGnuHashSymbolCount(const int fd, const std::size_t offset,
                                   std::size_t* symbol_count) {
  std::uint32_t header[4];
  if (!IsFileRangeReadable(fd, offset, sizeof(header)) ||
      !ReadFromOffsetExact(fd, header, sizeof(header), offset)) {
    return false;
  }
  std::size_t bloom_word_count;
  std::size_t bloom_size;
  std::size_t bucket_offset;
  if (!internal::CheckedMultiply(static_cast<std::size_t>(header[2]),
                                 sizeof(ElfW(Addr)) / sizeof(std::uint32_t),
                                 bloom_word_count) ||
      !internal::CheckedMultiply(bloom_word_count, sizeof(std::uint32_t),
                                 bloom_size) ||
      !internal::CheckedAdd(offset, sizeof(header), bucket_offset) ||
      !internal::CheckedAdd(bucket_offset, bloom_size, bucket_offset)) {
    return false;
  }
  std::size_t bucket_size;
  if (!internal::CheckedMultiply(static_cast<std::size_t>(header[0]),
                                 sizeof(std::uint32_t), bucket_size) ||
      !IsFileRangeReadable(fd, bucket_offset, bucket_size)) {
    return false;
  }
  std::size_t chain_offset;
  if (!internal::CheckedAdd(bucket_offset, bucket_size, chain_offset)) {
    return false;
  }
  std::size_t max_symbol = header[1];
  for (std::size_t i = 0; i < header[0]; ++i) {
    std::uint32_t bucket;
    std::size_t bucket_position;
    if (!internal::CheckedMultiply(i, sizeof(bucket), bucket_position) ||
        !internal::CheckedAdd(bucket_offset, bucket_position,
                              bucket_position) ||
        !ReadFromOffsetExact(fd, &bucket, sizeof(bucket), bucket_position)) {
      return false;
    }
    if (bucket < header[1]) {
      continue;
    }
    std::uint32_t symbol_index = bucket;
    std::uint32_t chain_index = bucket - header[1];
    while (true) {
      std::size_t chain_position;
      if (!internal::CheckedMultiply(static_cast<std::size_t>(chain_index),
                                     sizeof(std::uint32_t), chain_position) ||
          !internal::CheckedAdd(chain_offset, chain_position, chain_position)) {
        return false;
      }
      std::uint32_t chain_value;
      if (!ReadFromOffsetExact(fd, &chain_value, sizeof(chain_value),
                               chain_position)) {
        return false;
      }
      if (static_cast<std::size_t>(symbol_index) >= max_symbol) {
        std::size_t next_symbol;
        if (!internal::CheckedAdd(static_cast<std::size_t>(symbol_index),
                                  std::size_t{1}, next_symbol)) {
          return false;
        }
        max_symbol = next_symbol;
      }
      if ((chain_value & 1U) != 0) {
        break;
      }
      if (chain_index == std::numeric_limits<std::uint32_t>::max()) {
        return false;
      }
      ++chain_index;
    }
  }
  *symbol_count = max_symbol;
  return true;
}

static SymbolLookupResult GetDynamicSymbolTables(const int fd,
                                                 const ElfW(Ehdr) & elf_header,
                                                 ElfW(Shdr) * symtab,
                                                 ElfW(Shdr) * strtab) {
  std::size_t program_header_count;
  if (!GetProgramHeaderTableInfo(fd, elf_header, &program_header_count)) {
    return SymbolLookupResult::kMalformed;
  }
  if (program_header_count == 0) {
    return SymbolLookupResult::kNotFound;
  }

  ElfW(Phdr) dynamic_header{};
  bool has_dynamic_header = false;
  for (std::size_t i = 0; i < program_header_count; ++i) {
    ElfW(Phdr) program_header;
    if (!ReadProgramHeader(fd, i, program_header_count,
                           static_cast<std::size_t>(elf_header.e_phoff),
                           static_cast<std::size_t>(elf_header.e_phentsize),
                           &program_header)) {
      return SymbolLookupResult::kMalformed;
    }
    if (program_header.p_type == PT_DYNAMIC) {
      dynamic_header = program_header;
      has_dynamic_header = true;
      break;
    }
  }
  if (!has_dynamic_header || dynamic_header.p_filesz % sizeof(ElfW(Dyn)) != 0 ||
      !IsFileRangeReadable(
          fd, static_cast<std::uint64_t>(dynamic_header.p_offset),
          static_cast<std::uint64_t>(dynamic_header.p_filesz))) {
    return has_dynamic_header ? SymbolLookupResult::kMalformed
                              : SymbolLookupResult::kNotFound;
  }

  bool has_symtab = false;
  bool has_strtab = false;
  bool has_strtab_size = false;
  bool has_sysv_hash = false;
  bool has_gnu_hash = false;
  std::uint64_t symtab_address = 0;
  std::uint64_t strtab_address = 0;
  std::uint64_t hash_address = 0;
  std::uint64_t gnu_hash_address = 0;
  std::uint64_t strtab_size = 0;
  std::uint64_t symbol_entry_size = 0;
  const std::size_t dynamic_offset =
      static_cast<std::size_t>(dynamic_header.p_offset);
  const std::size_t dynamic_count =
      static_cast<std::size_t>(dynamic_header.p_filesz / sizeof(ElfW(Dyn)));
  for (std::size_t i = 0; i < dynamic_count; ++i) {
    std::size_t entry_offset;
    std::size_t offset;
    if (!internal::CheckedMultiply(i, sizeof(ElfW(Dyn)), entry_offset) ||
        !internal::CheckedAdd(dynamic_offset, entry_offset, offset)) {
      return SymbolLookupResult::kMalformed;
    }
    ElfW(Dyn) dynamic;
    if (!ReadFromOffsetExact(fd, &dynamic, sizeof(dynamic), offset)) {
      return SymbolLookupResult::kMalformed;
    }
    switch (dynamic.d_tag) {
      case DT_NULL:
        i = dynamic_count;
        break;
      case DT_SYMTAB:
        symtab_address = static_cast<std::uint64_t>(dynamic.d_un.d_ptr);
        has_symtab = true;
        break;
      case DT_SYMENT:
        symbol_entry_size = static_cast<std::uint64_t>(dynamic.d_un.d_val);
        break;
      case DT_STRTAB:
        strtab_address = static_cast<std::uint64_t>(dynamic.d_un.d_ptr);
        has_strtab = true;
        break;
      case DT_STRSZ:
        strtab_size = static_cast<std::uint64_t>(dynamic.d_un.d_val);
        has_strtab_size = true;
        break;
      case DT_HASH:
        hash_address = static_cast<std::uint64_t>(dynamic.d_un.d_ptr);
        has_sysv_hash = true;
        break;
#    if defined(DT_GNU_HASH)
      case DT_GNU_HASH:
        gnu_hash_address = static_cast<std::uint64_t>(dynamic.d_un.d_ptr);
        has_gnu_hash = true;
        break;
#    endif
      default:
        break;
    }
  }
  if (!has_symtab || !has_strtab || !has_strtab_size ||
      symbol_entry_size != sizeof(ElfW(Sym)) ||
      (!has_sysv_hash && !has_gnu_hash)) {
    return SymbolLookupResult::kNotFound;
  }

  std::size_t hash_offset;
  std::size_t gnu_hash_offset;
  std::size_t symbol_count;
  if (has_sysv_hash) {
    if (!ConvertDynamicAddressToFileOffset(
            fd, elf_header, program_header_count, hash_address,
            sizeof(std::uint32_t) * 2, &hash_offset) ||
        !ReadSysVHashSymbolCount(fd, hash_offset, &symbol_count)) {
      return SymbolLookupResult::kMalformed;
    }
  } else {
    if (!ConvertDynamicAddressToFileOffset(
            fd, elf_header, program_header_count, gnu_hash_address,
            sizeof(std::uint32_t) * 4, &gnu_hash_offset) ||
        !ReadGnuHashSymbolCount(fd, gnu_hash_offset, &symbol_count)) {
      return SymbolLookupResult::kMalformed;
    }
  }

  std::size_t symbol_table_size;
  if (!internal::CheckedMultiply(symbol_count, sizeof(ElfW(Sym)),
                                 symbol_table_size)) {
    return SymbolLookupResult::kMalformed;
  }
  std::size_t symbol_offset;
  std::size_t string_offset;
  if (!ConvertDynamicAddressToFileOffset(fd, elf_header, program_header_count,
                                         symtab_address, symbol_table_size,
                                         &symbol_offset) ||
      !ConvertDynamicAddressToFileOffset(fd, elf_header, program_header_count,
                                         strtab_address, strtab_size,
                                         &string_offset) ||
      strtab_size > std::numeric_limits<decltype(strtab->sh_size)>::max()) {
    return SymbolLookupResult::kMalformed;
  }
  symtab->sh_type = SHT_DYNSYM;
  symtab->sh_offset = symbol_offset;
  symtab->sh_size = symbol_table_size;
  symtab->sh_entsize = sizeof(ElfW(Sym));
  strtab->sh_type = SHT_STRTAB;
  strtab->sh_offset = string_offset;
  strtab->sh_size = strtab_size;
  return SymbolLookupResult::kFound;
}

// Read a symbol table and look for the symbol containing "pc". Return kFound
// and write the symbol name to out when successful. Return kNotFound when no
// symbol contains "pc". Return kMalformed for invalid file data.
// To keep stack consumption low, we would like this function to not get
// inlined.
NGLOG_ATTRIBUTE_NOINLINE
static SymbolLookupResult FindSymbol(std::uint64_t pc, const int fd, char* out,
                                     std::size_t out_size,
                                     std::uint64_t symbol_offset,
                                     const ElfW(Shdr) * strtab,
                                     const ElfW(Shdr) * symtab) {
  if (strtab == nullptr || symtab == nullptr ||
      symtab->sh_entsize != sizeof(ElfW(Sym)) ||
      symtab->sh_size % symtab->sh_entsize != 0) {
    return SymbolLookupResult::kMalformed;
  }
  if (!IsFileRangeReadable(fd, static_cast<std::uint64_t>(symtab->sh_offset),
                           static_cast<std::uint64_t>(symtab->sh_size)) ||
      !IsFileRangeReadable(fd, static_cast<std::uint64_t>(strtab->sh_offset),
                           static_cast<std::uint64_t>(strtab->sh_size))) {
    return SymbolLookupResult::kMalformed;
  }
  const std::size_t num_symbols = static_cast<std::size_t>(
      symtab->sh_size /
      static_cast<decltype(symtab->sh_size)>(symtab->sh_entsize));
  for (std::size_t i = 0; i < num_symbols;) {
    std::size_t symbol_offset_in_file;
    std::size_t offset;
    if (!internal::CheckedMultiply(i, sizeof(ElfW(Sym)),
                                   symbol_offset_in_file) ||
        !internal::CheckedAdd(static_cast<std::size_t>(symtab->sh_offset),
                              symbol_offset_in_file, offset)) {
      return SymbolLookupResult::kMalformed;
    }

    // If we are reading Elf64_Sym's, we want to limit this array to
    // 32 elements (to keep stack consumption low), otherwise we can
    // have a 64 element Elf32_Sym array.
#    if defined(__WORDSIZE) && __WORDSIZE == 64
    const std::size_t NUM_SYMBOLS = 32U;
#    else
    const std::size_t NUM_SYMBOLS = 64U;
#    endif

    // Read at most NUM_SYMBOLS symbols at once to save read() calls.
    ElfW(Sym) buf[NUM_SYMBOLS];
    std::size_t num_symbols_to_read = std::min(NUM_SYMBOLS, num_symbols - i);
    std::size_t bytes_to_read;
    if (!internal::CheckedMultiply(sizeof(buf[0]), num_symbols_to_read,
                                   bytes_to_read)) {
      return SymbolLookupResult::kMalformed;
    }
    const ssize_t len = ReadFromOffset(fd, &buf, bytes_to_read, offset);
    if (len <= 0 || static_cast<std::size_t>(len) % sizeof(buf[0]) != 0) {
      return SymbolLookupResult::kMalformed;
    }
    const std::size_t num_symbols_in_buf =
        static_cast<std::size_t>(len) / sizeof(buf[0]);
    for (std::size_t j = 0; j < num_symbols_in_buf; ++j) {
      const ElfW(Sym) & symbol = buf[j];
#    if defined(__WORDSIZE) && __WORDSIZE == 64
      const unsigned char symbol_type = ELF64_ST_TYPE(symbol.st_info);
#    else
      const unsigned char symbol_type = ELF32_ST_TYPE(symbol.st_info);
#    endif
      if (symbol_type == STT_TLS) {
        continue;
      }
      std::uint64_t start_address;
      std::uint64_t end_address;
      if (!internal::CheckedAdd(static_cast<std::uint64_t>(symbol.st_value),
                                symbol_offset, start_address) ||
          !internal::CheckedAdd(start_address,
                                static_cast<std::uint64_t>(symbol.st_size),
                                end_address)) {
        continue;
      }
      if (symbol.st_value != 0 &&  // Skip null value symbols.
          symbol.st_shndx != 0 &&  // Skip undefined symbols.
          ELF64_ST_TYPE(symbol.st_info) != STT_TLS && start_address <= pc &&
          pc < end_address) {
        if (symbol.st_name >= strtab->sh_size) {
          return SymbolLookupResult::kMalformed;
        }
        const auto name_bytes_available = strtab->sh_size - symbol.st_name;
        const std::size_t bytes_available =
            name_bytes_available > out_size
                ? out_size
                : static_cast<std::size_t>(name_bytes_available);
        std::size_t name_offset;
        if (!internal::CheckedAdd(static_cast<std::size_t>(strtab->sh_offset),
                                  static_cast<std::size_t>(symbol.st_name),
                                  name_offset) ||
            bytes_available == 0) {
          return SymbolLookupResult::kMalformed;
        }
        const ssize_t len1 =
            ReadFromOffset(fd, out, bytes_available, name_offset);
        if (len1 <= 0 ||
            std::memchr(out, '\0', static_cast<std::size_t>(len1)) == nullptr) {
          std::memset(out, 0, out_size);
          return SymbolLookupResult::kMalformed;
        }
        return SymbolLookupResult::kFound;  // Obtained the symbol name.
      }
    }
    i += num_symbols_in_buf;
  }
  return SymbolLookupResult::kNotFound;
}

// Get the symbol name of "pc" from the file pointed by "fd". Process both
// regular and dynamic symbol tables if necessary. Return kFound when
// successful, kNotFound when no symbol matches, and kMalformed for invalid
// file data.
static SymbolLookupResult GetSymbolFromObjectFile(const int fd,
                                                  std::uint64_t pc, char* out,
                                                  std::size_t out_size,
                                                  std::uint64_t base_address) {
  // Read the ELF header.
  ElfW(Ehdr) elf_header;
  if (!ReadFromOffsetExact(fd, &elf_header, sizeof(elf_header), 0)) {
    return SymbolLookupResult::kMalformed;
  }
  if (!IsSupportedElfHeader(elf_header)) {
    return SymbolLookupResult::kMalformed;
  }

  ElfW(Shdr) symtab, strtab;
  std::size_t section_count;
  std::size_t section_entry_size;
  if (!GetSectionTableInfo(fd, elf_header, &section_count, nullptr,
                           &section_entry_size)) {
    return SymbolLookupResult::kMalformed;
  }

  if (section_count != 0) {
    const std::size_t section_offset =
        static_cast<std::size_t>(elf_header.e_shoff);

    // Consult a regular symbol table first.
    const SectionLookupResult regular_result =
        GetSectionHeaderByType(fd, section_count, section_offset,
                               section_entry_size, SHT_SYMTAB, &symtab);
    if (regular_result == SectionLookupResult::kMalformed) {
      return SymbolLookupResult::kMalformed;
    }
    if (regular_result == SectionLookupResult::kFound) {
      if (symtab.sh_link >= section_count ||
          !ReadSectionHeader(fd, symtab.sh_link, section_count, section_offset,
                             section_entry_size, &strtab) ||
          strtab.sh_type != SHT_STRTAB) {
        return SymbolLookupResult::kMalformed;
      }
      const SymbolLookupResult result =
          FindSymbol(pc, fd, out, out_size, base_address, &strtab, &symtab);
      if (result == SymbolLookupResult::kFound ||
          result == SymbolLookupResult::kMalformed) {
        return result;
      }
    }

    // If the symbol is not found, then consult a dynamic symbol table.
    const SectionLookupResult dynamic_result =
        GetSectionHeaderByType(fd, section_count, section_offset,
                               section_entry_size, SHT_DYNSYM, &symtab);
    if (dynamic_result == SectionLookupResult::kMalformed) {
      return SymbolLookupResult::kMalformed;
    }
    if (dynamic_result == SectionLookupResult::kFound) {
      if (symtab.sh_link >= section_count ||
          !ReadSectionHeader(fd, symtab.sh_link, section_count, section_offset,
                             section_entry_size, &strtab) ||
          strtab.sh_type != SHT_STRTAB) {
        return SymbolLookupResult::kMalformed;
      }
      const SymbolLookupResult result =
          FindSymbol(pc, fd, out, out_size, base_address, &strtab, &symtab);
      if (result == SymbolLookupResult::kFound ||
          result == SymbolLookupResult::kMalformed) {
        return result;
      }
    }
  }

  const SymbolLookupResult dynamic_result =
      GetDynamicSymbolTables(fd, elf_header, &symtab, &strtab);
  if (dynamic_result == SymbolLookupResult::kMalformed) {
    return SymbolLookupResult::kMalformed;
  }
  if (dynamic_result == SymbolLookupResult::kFound) {
    return FindSymbol(pc, fd, out, out_size, base_address, &strtab, &symtab);
  }
  return SymbolLookupResult::kNotFound;
}

static bool GetProgramHeaderCountFromMemory(const int mem_fd,
                                            const std::uint64_t start_address,
                                            const ElfW(Ehdr) & elf_header,
                                            std::size_t* program_header_count) {
  std::size_t count = elf_header.e_phnum;
  if (elf_header.e_phnum == 0) {
    *program_header_count = 0;
    return true;
  }
  if (elf_header.e_phnum == PN_XNUM) {
    if (elf_header.e_shoff == 0 ||
        elf_header.e_shentsize < sizeof(ElfW(Shdr))) {
      return false;
    }
    std::uint64_t section_offset;
    if (!internal::CheckedAdd(start_address,
                              static_cast<std::uint64_t>(elf_header.e_shoff),
                              section_offset) ||
        section_offset > std::numeric_limits<std::size_t>::max()) {
      return false;
    }
    ElfW(Shdr) first_section;
    if (!ReadFromOffsetExact(mem_fd, &first_section, sizeof(first_section),
                             static_cast<std::size_t>(section_offset))) {
      return false;
    }
    count = static_cast<std::size_t>(first_section.sh_info);
  }
  if (count != 0 && elf_header.e_phentsize < sizeof(ElfW(Phdr))) {
    return false;
  }
  *program_header_count = count;
  return true;
}

static bool ReadProgramHeaderFromMemory(const int mem_fd,
                                        const std::uint64_t start_address,
                                        const ElfW(Ehdr) & elf_header,
                                        const std::size_t index,
                                        const std::size_t count,
                                        ElfW(Phdr) * out) {
  if (index >= count || elf_header.e_phentsize < sizeof(ElfW(Phdr))) {
    return false;
  }
  std::uint64_t entry_offset;
  std::uint64_t offset;
  if (!internal::CheckedMultiply(
          static_cast<std::uint64_t>(index),
          static_cast<std::uint64_t>(elf_header.e_phentsize), entry_offset) ||
      !internal::CheckedAdd(static_cast<std::uint64_t>(elf_header.e_phoff),
                            entry_offset, offset) ||
      !internal::CheckedAdd(start_address, offset, offset) ||
      offset > std::numeric_limits<std::size_t>::max()) {
    return false;
  }
  return ReadFromOffsetExact(mem_fd, out, sizeof(*out),
                             static_cast<std::size_t>(offset));
}

namespace {

// Helper class for reading lines from file.
//
// Note: we don't use ProcMapsIterator since the object is big (it has
// a 5k array member) and uses async-unsafe functions such as sscanf()
// and std::snprintf().
class LineReader {
 public:
  explicit LineReader(int fd, char* buf, size_t buf_len, size_t offset)
      : fd_(fd),
        buf_(buf),
        buf_len_(buf_len),
        offset_(offset),
        bol_(buf),
        eol_(buf),
        eod_(buf) {}

  // Read '\n'-terminated line from file.  On success, modify "bol"
  // and "eol", then return true.  Otherwise, return false.
  //
  // Note: if the last line doesn't end with '\n', the line will be
  // dropped.  It's an intentional behavior to make the code simple.
  bool ReadLine(const char** bol, const char** eol) {
    if (BufferIsEmpty()) {  // First time.
      const ssize_t num_bytes = ReadFromOffset(fd_, buf_, buf_len_, offset_);
      if (num_bytes <= 0) {  // EOF or error.
        return false;
      }
      offset_ += static_cast<size_t>(num_bytes);
      eod_ = buf_ + num_bytes;
      bol_ = buf_;
    } else {
      bol_ = eol_ + 1;  // Advance to the next line in the buffer.
      NGLOG_SAFE_ASSERT(bol_ <= eod_);  // "bol_" can point to "eod_".
      if (!HasCompleteLine()) {
        const auto incomplete_line_length = static_cast<size_t>(eod_ - bol_);
        // Move the trailing incomplete line to the beginning.
        memmove(buf_, bol_, incomplete_line_length);
        // Read text from file and append it.
        char* const append_pos = buf_ + incomplete_line_length;
        const size_t capacity_left = buf_len_ - incomplete_line_length;
        const ssize_t num_bytes =
            ReadFromOffset(fd_, append_pos, capacity_left, offset_);
        if (num_bytes <= 0) {  // EOF or error.
          return false;
        }
        offset_ += static_cast<size_t>(num_bytes);
        eod_ = append_pos + num_bytes;
        bol_ = buf_;
      }
    }
    eol_ = FindLineFeed();
    if (eol_ == nullptr) {  // '\n' not found.  Malformed line.
      return false;
    }
    *eol_ = '\0';  // Replace '\n' with '\0'.

    *bol = bol_;
    *eol = eol_;
    return true;
  }

  // Beginning of line.
  const char* bol() { return bol_; }

  // End of line.
  const char* eol() { return eol_; }

 private:
  LineReader(const LineReader&) = delete;
  void operator=(const LineReader&) = delete;

  char* FindLineFeed() {
    return reinterpret_cast<char*>(
        memchr(bol_, '\n', static_cast<size_t>(eod_ - bol_)));
  }

  bool BufferIsEmpty() { return buf_ == eod_; }

  bool HasCompleteLine() {
    return !BufferIsEmpty() && FindLineFeed() != nullptr;
  }

  const int fd_;
  char* const buf_;
  const size_t buf_len_;
  size_t offset_;
  char* bol_;
  char* eol_;
  const char* eod_;  // End of data in "buf_".
};
}  // namespace

// Place the hex number read from "start" into "*hex".  The pointer to
// the first non-hex character or "end" is returned.
static char* GetHex(const char* start, const char* end, uint64_t* hex) {
  *hex = 0;
  const char* p;
  for (p = start; p < end; ++p) {
    int ch = *p;
    if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') ||
        (ch >= 'a' && ch <= 'f')) {
      *hex = (*hex << 4U) |
             (ch < 'A' ? static_cast<uint64_t>(ch - '0') : (ch & 0xF) + 9U);
    } else {  // Encountered the first non-hex character.
      break;
    }
  }
  NGLOG_SAFE_ASSERT(p <= end);
  return const_cast<char*>(p);
}

// Searches for the object file (from /proc/self/maps) that contains
// the specified pc.  If found, sets |start_address| to the start address
// of where this object file is mapped in memory, sets the module base
// address into |base_address|, copies the object file name into
// |out_file_name|, and attempts to open the object file.  If the object
// file is opened successfully, returns the file descriptor.  Otherwise,
// returns -1.  |out_file_name_size| is the size of the file name buffer
// (including the null-terminator).
NGLOG_ATTRIBUTE_NOINLINE
static FileDescriptor OpenObjectFileContainingPcAndGetStartAddress(
    uint64_t pc, uint64_t& start_address, uint64_t& base_address,
    char* out_file_name, size_t out_file_name_size) {
  FileDescriptor maps_fd{
      FailureRetry([] { return open("/proc/self/maps", O_RDONLY); })};
  if (!maps_fd) {
    return nullptr;
  }

  FileDescriptor mem_fd{
      FailureRetry([] { return open("/proc/self/mem", O_RDONLY); })};
  if (!mem_fd) {
    return nullptr;
  }

  // Iterate over maps and look for the map containing the pc.  Then
  // look into the symbol tables inside.
  char buf[1024];  // Big enough for line of sane /proc/self/maps
  LineReader reader(maps_fd.get(), buf, sizeof(buf), 0);
  while (true) {
    const char* cursor;
    const char* eol;
    if (!reader.ReadLine(&cursor, &eol)) {  // EOF or malformed line.
      return nullptr;
    }

    // Start parsing line in /proc/self/maps.  Here is an example:
    //
    // 08048000-0804c000 r-xp 00000000 08:01 2142121    /bin/cat
    //
    // We want start address (08048000), end address (0804c000), flags
    // (r-xp) and file name (/bin/cat).

    // Read start address.
    cursor = GetHex(cursor, eol, &start_address);
    if (cursor == eol || *cursor != '-') {
      return nullptr;  // Malformed line.
    }
    ++cursor;  // Skip '-'.

    // Read end address.
    uint64_t end_address;
    cursor = GetHex(cursor, eol, &end_address);
    if (cursor == eol || *cursor != ' ') {
      return nullptr;  // Malformed line.
    }
    ++cursor;  // Skip ' '.

    // Read flags.  Skip flags until we encounter a space or eol.
    const char* const flags_start = cursor;
    while (cursor < eol && *cursor != ' ') {
      ++cursor;
    }
    // We expect at least four letters for flags (ex. "r-xp").
    if (cursor == eol || cursor < flags_start + 4) {
      return nullptr;  // Malformed line.
    }

    // Determine the base address by reading ELF headers in process memory.
    ElfW(Ehdr) ehdr;
    // Skip non-readable maps.
    if (flags_start[0] == 'r' &&
        ReadFromOffsetExact(mem_fd.get(), &ehdr, sizeof(ElfW(Ehdr)),
                            start_address) &&
        memcmp(ehdr.e_ident, ELFMAG, SELFMAG) == 0) {
      switch (ehdr.e_type) {
        case ET_EXEC:
          base_address = 0;
          break;
        case ET_DYN:
          // Find the segment containing file offset 0. This will correspond
          // to the ELF header that we just read. Normally this will have
          // virtual address 0, but this is not guaranteed. We must subtract
          // the virtual address from the address where the ELF header was
          // mapped to get the base address.
          //
          // If we fail to find a segment for file offset 0, use the address
          // of the ELF header as the base address.
          base_address = start_address;
          std::size_t program_header_count;
          if (!GetProgramHeaderCountFromMemory(mem_fd.get(), start_address,
                                               ehdr, &program_header_count)) {
            break;
          }
          for (std::size_t i = 0; i != program_header_count; ++i) {
            ElfW(Phdr) phdr;
            if (ReadProgramHeaderFromMemory(mem_fd.get(), start_address, ehdr,
                                            i, program_header_count, &phdr) &&
                phdr.p_type == PT_LOAD && phdr.p_offset == 0) {
              if (phdr.p_vaddr <= start_address) {
                base_address = start_address - phdr.p_vaddr;
              }
              break;
            }
          }
          break;
        default:
          // ET_REL or ET_CORE. These aren't directly executable, so they don't
          // affect the base address.
          break;
      }
    }

    // Check start and end addresses.
    if (start_address > pc || pc >= end_address) {
      continue;  // We skip this map.  PC isn't in this map.
    }

    // Check flags.  We are only interested in "r*x" maps.
    if (flags_start[0] != 'r' || flags_start[2] != 'x') {
      continue;  // We skip this map.
    }
    ++cursor;  // Skip ' '.

    // Read file offset.
    uint64_t file_offset;
    cursor = GetHex(cursor, eol, &file_offset);
    if (cursor == eol || *cursor != ' ') {
      return nullptr;  // Malformed line.
    }
    ++cursor;  // Skip ' '.

    // Skip to file name.  "cursor" now points to dev.  We need to
    // skip at least two spaces for dev and inode.
    int num_spaces = 0;
    while (cursor < eol) {
      if (*cursor == ' ') {
        ++num_spaces;
      } else if (num_spaces >= 2) {
        // The first non-space character after skipping two spaces
        // is the beginning of the file name.
        break;
      }
      ++cursor;
    }
    if (cursor == eol) {
      return nullptr;  // Malformed line.
    }

    strncpy(out_file_name, cursor, out_file_name_size);
    // Making sure |out_file_name| is always null-terminated.
    out_file_name[out_file_name_size - 1] = '\0';

    // Finally, "cursor" now points to file name of our interest.
    return FileDescriptor{
        FailureRetry([cursor] { return open(cursor, O_RDONLY); })};
  }
}

// POSIX doesn't define any async-signal safe function for converting
// an integer to ASCII. We'll have to define our own version.
// itoa_r() converts an (unsigned) integer to ASCII. It returns "buf", if the
// conversion was successful or nullptr otherwise. It never writes more than
// "sz" bytes. Output will be truncated as needed, and a NUL character is always
// appended.
// NOTE: code from sandbox/linux/seccomp-bpf/demo.cc.
static char* itoa_r(uintptr_t i, char* buf, size_t sz, unsigned base,
                    size_t padding) {
  // Make sure we can write at least one NUL byte.
  size_t n = 1;
  if (n > sz) {
    return nullptr;
  }

  if (base < 2 || base > 16) {
    buf[0] = '\000';
    return nullptr;
  }

  char* start = buf;

  // Loop until we have converted the entire number. Output at least one
  // character (i.e. '0').
  char* ptr = start;
  do {
    // Make sure there is still enough space left in our output buffer.
    if (++n > sz) {
      buf[0] = '\000';
      return nullptr;
    }

    // Output the next digit.
    *ptr++ = "0123456789abcdef"[i % base];
    i /= base;

    if (padding > 0) {
      padding--;
    }
  } while (i > 0 || padding > 0);

  // Terminate the output with a NUL character.
  *ptr = '\000';

  // Conversion to ASCII actually resulted in the digits being in reverse
  // order. We can't easily generate them in forward order, as we can't tell
  // the number of characters needed until we are done converting.
  // So, now, we reverse the string (except for the possible "-" sign).
  while (--ptr > start) {
    char ch = *ptr;
    *ptr = *start;
    *start++ = ch;
  }
  return buf;
}

// Safely appends string |source| to string |dest|.  Never writes past the
// buffer size |dest_size| and guarantees that |dest| is null-terminated.
static void SafeAppendString(const char* source, char* dest, size_t dest_size) {
  size_t dest_string_length = strlen(dest);
  NGLOG_SAFE_ASSERT(dest_string_length < dest_size);
  dest += dest_string_length;
  dest_size -= dest_string_length;
  strncpy(dest, source, dest_size);
  // Making sure |dest| is always null-terminated.
  dest[dest_size - 1] = '\0';
}

// Converts a 64-bit value into a hex string, and safely appends it to |dest|.
// Never writes past the buffer size |dest_size| and guarantees that |dest| is
// null-terminated.
static void SafeAppendHexNumber(uint64_t value, char* dest, size_t dest_size) {
  // 64-bit numbers in hex can have up to 16 digits.
  char buf[17] = {'\0'};
  SafeAppendString(itoa_r(value, buf, sizeof(buf), 16, 0), dest, dest_size);
}

// The implementation of our symbolization routine.  If it
// successfully finds the symbol containing "pc" and obtains the
// symbol name, returns true and write the symbol name to "out".
// Otherwise, returns false. If Callback function is installed via
// InstallSymbolizeCallback(), the function is also called in this function,
// and "out" is used as its output.
// To keep stack consumption low, we would like this function to not
// get inlined.
NGLOG_ATTRIBUTE_NOINLINE
static bool SymbolizeAndDemangle(void* pc, char* out, size_t out_size,
                                 SymbolizeOptions options,
                                 SymbolizedFrame* frame) {
  auto pc0 = reinterpret_cast<uintptr_t>(pc);
  uint64_t start_address = 0;
  uint64_t base_address = 0;
  FileDescriptor object_fd;

  constexpr std::size_t kMinimumOutputSize = 2;
  if (out_size < kMinimumOutputSize) {
    return false;
  }
  out[0] = '\0';
  SafeAppendString("(", out, out_size);

  if (g_symbolize_open_object_file_callback) {
    object_fd.reset(g_symbolize_open_object_file_callback(
        pc0, start_address, base_address, out + 1, out_size - 1));
  } else {
    object_fd = OpenObjectFileContainingPcAndGetStartAddress(
        pc0, start_address, base_address, out + 1, out_size - 1);
  }
  const bool object_name_available = out[1] != '\0';

#    if defined(PRINT_UNSYMBOLIZED_STACK_TRACES)
  {
#    else
  // Check whether a file name was returned.
  if (!object_fd) {
#    endif
    if (object_name_available) {
      // The object file containing PC was determined successfully however the
      // object file was not opened successfully.  This is still considered
      // success because the object file name and offset are known and tools
      // like asan_symbolize.py can be used for the symbolization.
      out[out_size - 1] = '\0';  // Making sure |out| is always null-terminated.
      SafeAppendString("+0x", out, out_size);
      SafeAppendHexNumber(pc0 - base_address, out, out_size);
      SafeAppendString(")", out, out_size);
      return true;
    }
    // Failed to determine the object file containing PC.  Bail out.
    return false;
  }
  int elf_type = FileGetElfType(object_fd.get());
  if (elf_type == -1) {
    return false;
  }
  if (g_symbolize_callback && (options & SymbolizeOptions::kNoLineNumbers) !=
                                  SymbolizeOptions::kNoLineNumbers) {
    // Run the call back if it's installed.
    // Note: relocation (and much of the rest of this code) may be wrong
    // for prelinked shared libraries. base_address (rather than
    // start_address, which is only the start of whichever /proc/self/maps
    // segment happens to contain "pc") is the same load bias
    // GetSymbolFromObjectFile() below uses to resolve "pc" against the
    // object's own symbol table, so it is what correctly maps "pc" back to
    // a file-relative address for PIE executables and shared libraries.
    uint64_t relocation = (elf_type == ET_DYN) ? base_address : 0;
    int num_bytes_written =
        g_symbolize_callback(object_fd.get(), pc, out, out_size, relocation);
    if (num_bytes_written > 0) {
      if (frame != nullptr) {
        // The callback overwrites "out" from the start with "<file>:<line>
        // " (see FormatAddr2LineOutput()), discarding the "(" appended
        // above. Trim the trailing space so the span covers exactly
        // "<file>:<line>".
        const auto written = static_cast<size_t>(num_bytes_written);
        frame->file_line_offset = 0;
        frame->file_line_length = written > 1 ? written - 1 : 0;
      }
      out += static_cast<size_t>(num_bytes_written);
      out_size -= static_cast<size_t>(num_bytes_written);
    }
  }
  const SymbolLookupResult symbol_result = GetSymbolFromObjectFile(
      object_fd.get(), pc0, out, out_size, base_address);
  if (symbol_result != SymbolLookupResult::kFound) {
    if (symbol_result == SymbolLookupResult::kMalformed) {
      return false;
    }
    if (object_name_available && !g_symbolize_callback) {
      // The object file containing PC was opened successfully however the
      // symbol was not found. The object may have been stripped. This is still
      // considered success because the object file name and offset are known
      // and tools like asan_symbolize.py can be used for the symbolization.
      out[out_size - 1] = '\0';  // Making sure |out| is always null-terminated.
      SafeAppendString("+0x", out, out_size);
      SafeAppendHexNumber(pc0 - base_address, out, out_size);
      SafeAppendString(")", out, out_size);
      return true;
    }
    return false;
  }

  // Symbolization succeeded.  Now we try to demangle the symbol.
  DemangleInplace(out, out_size);
  return true;
}

}  // namespace tools
}  // namespace nglog

#  elif defined(NGLOG_OS_MACOSX) && defined(HAVE_DLADDR)

#    include <dlfcn.h>

#    include <cstring>

namespace nglog {
inline namespace tools {

NGLOG_ATTRIBUTE_NOINLINE
static bool SymbolizeAndDemangle(void* pc, char* out, size_t out_size,
                                 SymbolizeOptions /*options*/,
                                 SymbolizedFrame* /*frame*/) {
  Dl_info info;
  if (dladdr(pc, &info)) {
    if (info.dli_sname) {
      if (strlen(info.dli_sname) < out_size) {
        strcpy(out, info.dli_sname);
        // Symbolization succeeded.  Now we try to demangle the symbol.
        DemangleInplace(out, out_size);
        return true;
      }
    }
  }
  return false;
}

}  // namespace tools
}  // namespace nglog

#  elif defined(NGLOG_OS_WINDOWS) && defined(HAVE_ADDR2LINE)

// Resolves both the symbol name and the file/line via addr2line, since
// DbgHelp cannot read the DWARF debug info a MinGW build emits by
// default. Known MinGW/binutils linker quirk, observed with
// x86_64-w64-mingw32-ld 2.46: when the address being
// resolved falls in the same translation unit as main(), the linker
// appears to mis-merge .debug_ranges across compilation units, and
// addr2line reports "??" with an unrelated file. Addresses in other
// translation units resolve correctly. This is a toolchain limitation,
// not addressed here.

#    include <windows.h>

#    include "addr2line.h"

namespace nglog {
inline namespace tools {

namespace {
// Room for the worst-case UTF-8 expansion of a MAX_PATH UTF-16 string.
constexpr int kMaxObjectPathUtf8Length = MAX_PATH * 3;
}  // namespace

NGLOG_ATTRIBUTE_NOINLINE
static bool SymbolizeAndDemangle(void* pc, char* out, size_t out_size,
                                 SymbolizeOptions options,
                                 SymbolizedFrame* frame) {
  HMODULE module = nullptr;

  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(pc), &module)) {
    return false;
  }

  wchar_t wide_path[MAX_PATH];
  const DWORD wide_len = GetModuleFileNameW(module, wide_path, MAX_PATH);

  if (wide_len == 0 || wide_len >= MAX_PATH) {
    return false;
  }

  char object_path[kMaxObjectPathUtf8Length];
  const int narrow_len =
      WideCharToMultiByte(CP_UTF8, 0, wide_path, -1, object_path,
                          sizeof(object_path), nullptr, nullptr);

  if (narrow_len <= 0) {
    return false;
  }

  // addr2line resolves addresses against the file's own coordinate
  // system, i.e. relative to the PE's declared ImageBase, not the
  // (possibly ASLR-relocated) address the module actually loaded at. A
  // module's HMODULE is its runtime load base, and that same base
  // address is where the loaded image's own PE header sits in memory,
  // making its declared ImageBase readable directly from the running
  // process without any extra API calls.
  const auto runtime_base = reinterpret_cast<uintptr_t>(module);
  const auto* dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
  const auto* nt_headers = reinterpret_cast<const IMAGE_NT_HEADERS*>(
      reinterpret_cast<const std::uint8_t*>(module) + dos_header->e_lfanew);
  const auto image_base =
      static_cast<uintptr_t>(nt_headers->OptionalHeader.ImageBase);
  const auto relocation = runtime_base - image_base;
  return ResolveFunctionAndLine(object_path, pc, relocation, out, out_size,
                                options, frame);
}

}  // namespace tools
}  // namespace nglog

#  elif defined(NGLOG_OS_WINDOWS) || defined(NGLOG_OS_CYGWIN)

// clang-format off
#    include <windows.h>  // Must come before <dbghelp.h>
#    include <dbghelp.h>
// clang-format on

namespace nglog {
inline namespace tools {

namespace {

class SymInitializer final {
 public:
  HANDLE process;
  bool ready;
  SymInitializer() : process(GetCurrentProcess()), ready(false) {
    // Initialize the symbol handler.
    // https://msdn.microsoft.com/en-us/library/windows/desktop/ms680344(v=vs.85).aspx
    // Defer symbol loading.
    // We do not request undecorated symbols with SYMOPT_UNDNAME
    // because the mangling library calls UnDecorateSymbolName.
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    if (SymInitialize(process, nullptr, true)) {
      ready = true;
    }
  }
  ~SymInitializer() {
    SymCleanup(process);
    // We do not need to close `HANDLE process` because it's a "pseudo handle."
  }

  SymInitializer(const SymInitializer&) = delete;
  SymInitializer& operator=(const SymInitializer&) = delete;
  SymInitializer(SymInitializer&&) = delete;
  SymInitializer& operator=(SymInitializer&&) = delete;
};

}  // namespace

NGLOG_ATTRIBUTE_NOINLINE
static bool SymbolizeAndDemangle(void* pc, char* out, size_t out_size,
                                 SymbolizeOptions options,
                                 SymbolizedFrame* frame) {
  const static SymInitializer symInitializer;
  if (!symInitializer.ready) {
    return false;
  }
  // Resolve symbol information from address.
  // https://msdn.microsoft.com/en-us/library/windows/desktop/ms680578(v=vs.85).aspx
  char buf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
  SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buf);
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = MAX_SYM_NAME;
  // We use the ANSI version to ensure the string type is always `char *`.
  // This could break if a symbol has Unicode in it.
  BOOL ret = SymFromAddr(symInitializer.process, reinterpret_cast<DWORD64>(pc),
                         0, symbol);
  std::size_t namelen = static_cast<size_t>(symbol->NameLen);
  if (ret && namelen < out_size) {
    std::strncpy(out, symbol->Name, namelen);
    out[namelen] = '\0';

    DWORD displacement;
    IMAGEHLP_LINE64 line = {};
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    BOOL found = FALSE;

    if ((options & SymbolizeOptions::kNoLineNumbers) !=
        SymbolizeOptions::kNoLineNumbers) {
      found = SymGetLineFromAddr64(symInitializer.process,
                                   reinterpret_cast<DWORD64>(pc), &displacement,
                                   &line);
    }

    // Symbolization succeeded.  Now we try to demangle the symbol.
    DemangleInplace(out, out_size);

    if (found) {
      std::size_t fnlen = std::strlen(line.FileName);
      // Determine the number of digits (base 10) necessary to represent the
      // line number
      std::size_t digits = 1;  // At least one digit required
      for (DWORD value = line.LineNumber; (value /= 10) != 0; ++digits) {
      }
      constexpr std::size_t kColonLength = 1;
      constexpr std::size_t kSeparatorLength = 1;
      const std::size_t file_line_length = fnlen + kColonLength + digits;
      const std::size_t function_length = std::strlen(out);
      const std::size_t required_length =
          file_line_length + kSeparatorLength + function_length + 1;

      if (required_length <= out_size) {
        std::memmove(out + file_line_length + kSeparatorLength, out,
                     function_length + 1);
        const int written = std::snprintf(out, out_size, "%s:%lu ",
                                          line.FileName, line.LineNumber);
        if (frame != nullptr &&
            written == static_cast<int>(file_line_length + kSeparatorLength)) {
          frame->file_line_offset = 0;
          frame->file_line_length = file_line_length;
        }
      }
    }

    return true;
  }
  return false;
}

}  // namespace tools
}  // namespace nglog

#  else
#    error BUG: HAVE_SYMBOLIZE was wrongly set
#  endif

namespace nglog {
inline namespace tools {

bool Symbolize(void* pc, char* out, size_t out_size, SymbolizeOptions options,
               SymbolizedFrame* frame) {
  if (frame != nullptr) {
    *frame = SymbolizedFrame{};
  }
  return SymbolizeAndDemangle(pc, out, out_size, options, frame);
}

}  // namespace tools
}  // namespace nglog

#endif
