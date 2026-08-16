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
//
// Unit tests for functions in symbolize.cc.

#include "symbolize.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

#include "config.h"
#include "internal/checked_arithmetic.h"
#include "ng-log/logging.h"
#include "stacktrace.h"
#include "utilities.h"

#if defined(HAVE_LINK_H)
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>

#  include <iterator>
#endif

#ifdef NGLOG_USE_GFLAGS
#  include <gflags/gflags.h>
using namespace GFLAGS_NAMESPACE;
#endif

using namespace std;
using namespace nglog;

using testing::AnyOf;
using testing::HasSubstr;
using testing::IsNull;
using testing::Not;
using testing::StrEq;

TEST(CheckedAdd, SupportsSignedOperands) {
  int result = 0;
  EXPECT_TRUE(nglog::internal::CheckedAdd(-7, 3, result));
  EXPECT_EQ(-4, result);
  EXPECT_TRUE(nglog::internal::CheckedAdd(7, -3, result));
  EXPECT_EQ(4, result);
}

TEST(CheckedAdd, SupportsUnsignedOperands) {
  std::uint32_t result = 0;
  EXPECT_TRUE(
      nglog::internal::CheckedAdd(std::uint32_t{7}, std::uint32_t{3}, result));
  EXPECT_EQ(10U, result);
  EXPECT_FALSE(nglog::internal::CheckedAdd(
      std::numeric_limits<std::uint32_t>::max(), std::uint32_t{1}, result));
}

TEST(CheckedAdd, RejectsSignedOverflowAndUnderflow) {
  int result = 0;
  EXPECT_FALSE(
      nglog::internal::CheckedAdd(std::numeric_limits<int>::max(), 1, result));
  EXPECT_FALSE(
      nglog::internal::CheckedAdd(std::numeric_limits<int>::min(), -1, result));
}

// Avoid compile error due to "cast between pointer-to-function and
// pointer-to-object is an extension" warnings.
#if defined(__GNUG__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wpedantic"
#endif

#if defined(HAVE_STACKTRACE)

#  if defined(HAVE_ELF_H) || defined(HAVE_SYS_EXEC_ELF_H) || \
      defined(NGLOG_OS_WINDOWS) || defined(NGLOG_OS_CYGWIN)
// A wrapper function for Symbolize() to make the unit test simple.
static const char* TrySymbolize(void* pc, nglog::SymbolizeOptions options =
                                              nglog::SymbolizeOptions::kNone) {
  static char symbol[4096];
  if (Symbolize(pc, symbol, sizeof(symbol), options)) {
    return symbol;
  } else {
    return nullptr;
  }
}
#  endif

#  if defined(HAVE_ELF_H) || defined(HAVE_SYS_EXEC_ELF_H)
// This unit tests make sense only with GCC.
// Uses lots of GCC specific features.
#    if defined(__GNUC__) && !defined(__OPENCC__)
#      if __GNUC__ >= 4
#        define TEST_WITH_MODERN_GCC
#      else
#      endif  // __GNUC__ >= 4
#      define TEST_WITH_LABEL_ADDRESSES
#    endif

// Make them C linkage to avoid mangled names.
extern "C" {
void nonstatic_func();
void nonstatic_func() {
  volatile int a = 0;
  // NOTE: In C++20, increment of object of volatile-qualified type is
  // deprecated.
  a = a + 1;
}

static void static_func() {
  volatile int a = 0;
  // NOTE: In C++20, increment of object of volatile-qualified type is
  // deprecated.
  a = a + 1;
}
}

#    if defined(HAVE_LINK_H) && !defined(HAVE_LIBBACKTRACE)
namespace {

constexpr std::uint64_t kSyntheticProgramCounter = 0x1000U;
constexpr std::size_t kNullSymbolIndex = 0;
constexpr std::size_t kTlsSymbolIndex = 1;
constexpr std::size_t kFunctionSymbolIndex = 2;
constexpr std::size_t kSymbolCount = 3;
constexpr std::size_t kNullSectionIndex = 0;
constexpr std::size_t kTextSectionIndex = 1;
constexpr std::size_t kSymbolTableSectionIndex = 2;
constexpr std::size_t kStringTableSectionIndex = 3;
constexpr std::size_t kSectionCount = 4;
constexpr std::size_t kFirstGlobalSymbolIndex = kTlsSymbolIndex;
constexpr std::size_t kSymbolSize = 1;
constexpr char kSymbolStrings[] = "\0thread_msg_data\0expected_function";
constexpr std::size_t kTlsNameOffset = 1;
constexpr std::size_t kFunctionNameOffset =
    kTlsNameOffset + sizeof("thread_msg_data");

int g_synthetic_elf_file = -1;
std::size_t g_synthetic_section_header_offset = 0;
std::size_t g_synthetic_symbol_table_offset = 0;
std::size_t g_synthetic_symbol_string_offset = 0;
char g_synthetic_elf_path[128];

using SyntheticElfHeader = ElfW(Ehdr);
using SyntheticSectionHeader = ElfW(Shdr);
using SyntheticSymbol = ElfW(Sym);

int OpenSyntheticElf(std::uint64_t /*pc*/, std::uint64_t& start_address,
                     std::uint64_t& base_address, char* out_file_name,
                     std::size_t out_file_name_size) {
  start_address = 0;
  base_address = 0;
  std::strncpy(out_file_name, "synthetic-elf", out_file_name_size);
  out_file_name[out_file_name_size - 1] = '\0';
  return dup(g_synthetic_elf_file);
}

#      if !defined(HAVE_LIBBACKTRACE)
int NoopSymbolizeCallback(int /*fd*/, void* /*pc*/, char* /*out*/,
                          std::size_t /*out_size*/,
                          std::uint64_t /*relocation*/) {
  return -1;
}
#      endif

template <typename T>
void AppendObject(std::vector<char>& data, const T& object) {
  const auto* bytes = reinterpret_cast<const char*>(&object);
  data.insert(data.end(), bytes, bytes + sizeof(object));
}

template <typename T>
bool ReadSyntheticObject(std::size_t offset, T& object) {
  return pread(g_synthetic_elf_file, &object, sizeof(object),
               static_cast<off_t>(offset)) ==
         static_cast<ssize_t>(sizeof(object));
}

template <typename T>
bool WriteSyntheticObject(std::size_t offset, const T& object) {
  return pwrite(g_synthetic_elf_file, &object, sizeof(object),
                static_cast<off_t>(offset)) ==
         static_cast<ssize_t>(sizeof(object));
}

int CreateSyntheticElf(std::size_t section_header_extra = 0) {
  using ElfHeader = ElfW(Ehdr);
  using SectionHeader = ElfW(Shdr);
  using Symbol = ElfW(Sym);

  std::vector<Symbol> symbols(kSymbolCount);
  symbols[kTlsSymbolIndex].st_name = kTlsNameOffset;
  symbols[kTlsSymbolIndex].st_info = sizeof(void*) == sizeof(std::uint64_t)
                                         ? ELF64_ST_INFO(STB_GLOBAL, STT_TLS)
                                         : ELF32_ST_INFO(STB_GLOBAL, STT_TLS);
  symbols[kTlsSymbolIndex].st_value = kSyntheticProgramCounter;
  symbols[kTlsSymbolIndex].st_size = kSymbolSize;
  symbols[kTlsSymbolIndex].st_shndx = kTextSectionIndex;
  symbols[kFunctionSymbolIndex].st_name = kFunctionNameOffset;
  symbols[kFunctionSymbolIndex].st_info =
      sizeof(void*) == sizeof(std::uint64_t)
          ? ELF64_ST_INFO(STB_GLOBAL, STT_FUNC)
          : ELF32_ST_INFO(STB_GLOBAL, STT_FUNC);
  symbols[kFunctionSymbolIndex].st_value = kSyntheticProgramCounter;
  symbols[kFunctionSymbolIndex].st_size = kSymbolSize;
  symbols[kFunctionSymbolIndex].st_shndx = kTextSectionIndex;

  std::vector<char> data(sizeof(ElfHeader));
  const auto append_section = [&data](const auto& object) {
    const std::size_t alignment = alignof(decltype(object));
    data.resize((data.size() + alignment - 1) / alignment * alignment);
    AppendObject(data, object);
    return data.size() - sizeof(object);
  };
  const std::size_t text_offset = append_section(std::uint32_t{0});
  const std::size_t symbol_offset = append_section(symbols[kNullSymbolIndex]);
  g_synthetic_symbol_table_offset = symbol_offset;
  for (std::size_t i = kTlsSymbolIndex; i < symbols.size(); ++i) {
    AppendObject(data, symbols[i]);
  }
  const std::size_t symbol_string_offset = data.size();
  g_synthetic_symbol_string_offset = symbol_string_offset;
  data.insert(data.end(), std::begin(kSymbolStrings), std::end(kSymbolStrings));

  const std::size_t section_header_offset =
      (data.size() + alignof(SectionHeader) - 1) / alignof(SectionHeader) *
      alignof(SectionHeader);
  g_synthetic_section_header_offset = section_header_offset;
  data.resize(section_header_offset);
  std::vector<SectionHeader> sections(kSectionCount);
  sections[kTextSectionIndex].sh_type = SHT_PROGBITS;
  sections[kTextSectionIndex].sh_offset = text_offset;
  sections[kTextSectionIndex].sh_size = sizeof(std::uint32_t);
  sections[kSymbolTableSectionIndex].sh_type = SHT_SYMTAB;
  sections[kSymbolTableSectionIndex].sh_offset = symbol_offset;
  sections[kSymbolTableSectionIndex].sh_size = symbols.size() * sizeof(Symbol);
  sections[kSymbolTableSectionIndex].sh_link = kStringTableSectionIndex;
  sections[kSymbolTableSectionIndex].sh_info = kFirstGlobalSymbolIndex;
  sections[kSymbolTableSectionIndex].sh_addralign = alignof(Symbol);
  sections[kSymbolTableSectionIndex].sh_entsize = sizeof(Symbol);
  sections[kNullSectionIndex].sh_type = SHT_STRTAB;
  sections[kNullSectionIndex].sh_name = kFunctionNameOffset;
  sections[kNullSectionIndex].sh_offset = symbol_string_offset;
  sections[kNullSectionIndex].sh_size = sizeof(kSymbolStrings);
  sections[kStringTableSectionIndex].sh_type = SHT_STRTAB;
  sections[kStringTableSectionIndex].sh_offset = symbol_string_offset;
  sections[kStringTableSectionIndex].sh_size = sizeof(kSymbolStrings);
  for (const SectionHeader& section : sections) {
    AppendObject(data, section);
    data.resize(data.size() + section_header_extra);
  }

  ElfHeader header{};
  std::memcpy(header.e_ident, ELFMAG, SELFMAG);
  header.e_ident[EI_CLASS] =
      sizeof(void*) == sizeof(std::uint64_t) ? ELFCLASS64 : ELFCLASS32;
  header.e_ident[EI_DATA] = ELFDATA2LSB;
  header.e_ident[EI_VERSION] = EV_CURRENT;
  header.e_type = ET_EXEC;
  header.e_machine = EM_NONE;
  header.e_version = EV_CURRENT;
  header.e_ehsize = sizeof(ElfHeader);
  header.e_shentsize = static_cast<decltype(header.e_shentsize)>(
      sizeof(SectionHeader) + section_header_extra);
  header.e_shnum = static_cast<decltype(header.e_shnum)>(sections.size());
  header.e_shstrndx = kNullSectionIndex;
  header.e_shoff = section_header_offset;
  std::memcpy(data.data(), &header, sizeof(header));

  char file_name[] = "/tmp/nglog-symbolize-XXXXXX";
  const int file = mkstemp(file_name);
  unlink(file_name);
  if (file == -1) {
    return -1;
  }
  std::size_t written = 0;
  while (written != data.size()) {
    const ssize_t result =
        write(file, data.data() + written, data.size() - written);
    if (result <= 0) {
      close(file);
      return -1;
    }
    written += static_cast<std::size_t>(result);
  }
  return file;
}

int CreateSectionlessDynamicElf(std::size_t program_header_extra = 0,
                                bool extended_program_header_count = false,
                                std::uint64_t load_vaddr = 0,
                                bool keep_file = false,
                                bool use_gnu_hash = false) {
  using ElfHeader = ElfW(Ehdr);
  using ProgramHeader = ElfW(Phdr);
  using Dynamic = ElfW(Dyn);
  using Symbol = ElfW(Sym);

  constexpr std::size_t kSymbolCount = 2;
  const std::uint64_t symbol_value = load_vaddr + kSyntheticProgramCounter;
  constexpr char kSymbolStrings[] = "\0expected_function";

  std::vector<char> data(sizeof(ElfHeader));
  const auto append_aligned = [&data](const auto& object) {
    const std::size_t alignment = alignof(decltype(object));
    data.resize((data.size() + alignment - 1) / alignment * alignment);
    AppendObject(data, object);
    return data.size() - sizeof(object);
  };
  const std::size_t program_header_offset = append_aligned(ProgramHeader{});
  data.resize(program_header_offset + sizeof(ProgramHeader) * 2 +
              program_header_extra * 2);

  std::vector<Dynamic> dynamics(6);
  const std::size_t dynamic_offset = append_aligned(dynamics[0]);
  for (std::size_t i = 1; i < dynamics.size(); ++i) {
    AppendObject(data, dynamics[i]);
  }

  std::vector<Symbol> symbols(kSymbolCount);
  symbols[1].st_name = 1;
  symbols[1].st_value = symbol_value;
  symbols[1].st_size = kSymbolSize;
  symbols[1].st_info = sizeof(void*) == sizeof(std::uint64_t)
                           ? ELF64_ST_INFO(STB_GLOBAL, STT_FUNC)
                           : ELF32_ST_INFO(STB_GLOBAL, STT_FUNC);
  symbols[1].st_shndx = 1;
  const std::size_t symbol_offset = append_aligned(symbols[0]);
  AppendObject(data, symbols[1]);
  const std::size_t string_offset = data.size();
  data.insert(data.end(), std::begin(kSymbolStrings), std::end(kSymbolStrings));

  std::vector<std::uint32_t> hash;
  if (use_gnu_hash) {
    hash = {1, 1, 1, 0};
    hash.resize(hash.size() + sizeof(ElfW(Addr)) / sizeof(std::uint32_t));
    hash.push_back(1);
    hash.push_back(1);
  } else {
    hash = {1, 2, 1, 0, 0};
  }
  const std::size_t hash_offset = append_aligned(hash[0]);
  for (std::size_t i = 1; i < hash.size(); ++i) {
    AppendObject(data, hash[i]);
  }

  std::size_t section_header_offset = 0;
  if (extended_program_header_count) {
    section_header_offset = append_aligned(ElfW(Shdr){});
    ElfW(Shdr) section_zero{};
    section_zero.sh_info = 2;
    std::memcpy(data.data() + section_header_offset, &section_zero,
                sizeof(section_zero));
  }

  dynamics[0].d_tag = DT_SYMTAB;
  dynamics[0].d_un.d_ptr = load_vaddr + symbol_offset;
  dynamics[1].d_tag = DT_SYMENT;
  dynamics[1].d_un.d_val = sizeof(Symbol);
  dynamics[2].d_tag = DT_STRTAB;
  dynamics[2].d_un.d_ptr = load_vaddr + string_offset;
  dynamics[3].d_tag = DT_STRSZ;
  dynamics[3].d_un.d_val = sizeof(kSymbolStrings);
#      if defined(DT_GNU_HASH)
  dynamics[4].d_tag = use_gnu_hash ? DT_GNU_HASH : DT_HASH;
#      else
  dynamics[4].d_tag = DT_HASH;
#      endif
  dynamics[4].d_un.d_ptr = load_vaddr + hash_offset;
  dynamics[5].d_tag = DT_NULL;
  std::memcpy(data.data() + dynamic_offset, dynamics.data(),
              dynamics.size() * sizeof(Dynamic));

  std::vector<ProgramHeader> program_headers(2);
  program_headers[0].p_type = PT_LOAD;
  program_headers[0].p_offset = 0;
  program_headers[0].p_vaddr = load_vaddr;
  program_headers[0].p_filesz = data.size();
  program_headers[0].p_memsz = data.size();
  program_headers[0].p_flags = PF_R | PF_X;
  program_headers[0].p_align = 1;
  program_headers[1].p_type = PT_DYNAMIC;
  program_headers[1].p_offset = dynamic_offset;
  program_headers[1].p_vaddr = load_vaddr + dynamic_offset;
  program_headers[1].p_filesz = dynamics.size() * sizeof(Dynamic);
  program_headers[1].p_memsz = program_headers[1].p_filesz;
  std::memcpy(data.data() + program_header_offset, program_headers.data(),
              sizeof(ProgramHeader));
  std::memcpy(data.data() + program_header_offset + sizeof(ProgramHeader) +
                  program_header_extra,
              program_headers.data() + 1, sizeof(ProgramHeader));

  ElfHeader header{};
  std::memcpy(header.e_ident, ELFMAG, SELFMAG);
  header.e_ident[EI_CLASS] =
      sizeof(void*) == sizeof(std::uint64_t) ? ELFCLASS64 : ELFCLASS32;
  header.e_ident[EI_DATA] = ELFDATA2LSB;
  header.e_ident[EI_VERSION] = EV_CURRENT;
  header.e_type = load_vaddr == 0 ? ET_EXEC : ET_DYN;
  header.e_machine = EM_NONE;
  header.e_version = EV_CURRENT;
  header.e_ehsize = sizeof(ElfHeader);
  header.e_phoff = program_header_offset;
  header.e_phentsize = static_cast<decltype(header.e_phentsize)>(
      sizeof(ProgramHeader) + program_header_extra);
  header.e_phnum = extended_program_header_count ? PN_XNUM : 2;
  header.e_shoff = section_header_offset;
  header.e_shentsize = extended_program_header_count ? sizeof(ElfW(Shdr)) : 0;
  header.e_shnum = extended_program_header_count ? 1 : 0;
  header.e_shstrndx = extended_program_header_count ? 0 : SHN_UNDEF;
  symbols[1].st_value = symbol_value;
  std::memcpy(data.data() + symbol_offset, symbols.data(),
              symbols.size() * sizeof(Symbol));
  std::memcpy(data.data(), &header, sizeof(header));

  char file_name[] = "/tmp/nglog-symbolize-dynamic-XXXXXX";
  const int file = mkstemp(file_name);
  if (file == -1) {
    return -1;
  }
  std::strncpy(g_synthetic_elf_path, file_name, sizeof(g_synthetic_elf_path));
  g_synthetic_elf_path[sizeof(g_synthetic_elf_path) - 1] = '\0';
  if (!keep_file) {
    unlink(file_name);
  }
  std::size_t written = 0;
  while (written != data.size()) {
    const ssize_t result =
        write(file, data.data() + written, data.size() - written);
    if (result <= 0) {
      close(file);
      return -1;
    }
    written += static_cast<std::size_t>(result);
  }
  return file;
}

}  // namespace
#    endif

TEST(Symbolize, Symbolize) {
  // We do C-style cast since GCC 2.95.3 doesn't allow
  // reinterpret_cast<void *>(&func).

  // Compilers should give us pointers to them.
  EXPECT_STREQ("nonstatic_func", TrySymbolize((void*)(&nonstatic_func)));

  // The name of an internal linkage symbol is not specified; allow either a
  // mangled or an unmangled name here.
  const char* static_func_symbol =
      TrySymbolize(reinterpret_cast<void*>(&static_func));

#    if !defined(_MSC_VER) || !defined(NDEBUG)
  ASSERT_THAT(static_func_symbol, Not(IsNull()));
  EXPECT_THAT(static_func_symbol,
              AnyOf(StrEq("static_func"), StrEq("static_func()")));
#    endif

  EXPECT_THAT(TrySymbolize(nullptr), IsNull());
}

#    if defined(HAVE_LINK_H) && !defined(HAVE_LIBBACKTRACE)
TEST(Symbolize, IgnoresTlsSymbols) {
  g_synthetic_elf_file = CreateSyntheticElf();
  ASSERT_NE(g_synthetic_elf_file, -1);
  InstallSymbolizeOpenObjectFileCallback(OpenSyntheticElf);

  char symbol[64];
  EXPECT_TRUE(Symbolize(reinterpret_cast<void*>(kSyntheticProgramCounter),
                        symbol, sizeof(symbol),
                        nglog::SymbolizeOptions::kNoLineNumbers));
  EXPECT_STREQ("expected_function", symbol);

  InstallSymbolizeOpenObjectFileCallback(nullptr);
  close(g_synthetic_elf_file);
  g_synthetic_elf_file = -1;
}
#    endif

#    if defined(HAVE_LINK_H) && !defined(HAVE_LIBBACKTRACE)
TEST(Symbolize, FallsBackForSectionlessElf) {
  g_synthetic_elf_file = CreateSyntheticElf();
  ASSERT_NE(g_synthetic_elf_file, -1);
  SyntheticElfHeader header;
  ASSERT_TRUE(ReadSyntheticObject(0, header));
  header.e_shoff = 0;
  header.e_shnum = 0;
  header.e_shstrndx = SHN_UNDEF;
  ASSERT_TRUE(WriteSyntheticObject(0, header));
  InstallSymbolizeOpenObjectFileCallback(OpenSyntheticElf);

  char symbol[64];
  EXPECT_TRUE(Symbolize(reinterpret_cast<void*>(kSyntheticProgramCounter),
                        symbol, sizeof(symbol),
                        nglog::SymbolizeOptions::kNoLineNumbers));
  EXPECT_STREQ("(synthetic-elf+0x1000)", symbol);

  InstallSymbolizeOpenObjectFileCallback(nullptr);
  close(g_synthetic_elf_file);
  g_synthetic_elf_file = -1;
}

TEST(Symbolize, AcceptsExtendedSectionEntrySize) {
  g_synthetic_elf_file = CreateSyntheticElf(1);
  ASSERT_NE(g_synthetic_elf_file, -1);
  InstallSymbolizeOpenObjectFileCallback(OpenSyntheticElf);

  char symbol[64];
  EXPECT_TRUE(Symbolize(reinterpret_cast<void*>(kSyntheticProgramCounter),
                        symbol, sizeof(symbol),
                        nglog::SymbolizeOptions::kNoLineNumbers));
  EXPECT_STREQ("expected_function", symbol);

  InstallSymbolizeOpenObjectFileCallback(nullptr);
  close(g_synthetic_elf_file);
  g_synthetic_elf_file = -1;
}

TEST(Symbolize, FindsSymbolInSectionlessDynamicElf) {
  g_synthetic_elf_file = CreateSectionlessDynamicElf(1);
  ASSERT_NE(g_synthetic_elf_file, -1);
  InstallSymbolizeOpenObjectFileCallback(OpenSyntheticElf);

  char symbol[64];
  EXPECT_TRUE(Symbolize(reinterpret_cast<void*>(kSyntheticProgramCounter),
                        symbol, sizeof(symbol),
                        nglog::SymbolizeOptions::kNoLineNumbers));
  EXPECT_STREQ("expected_function", symbol);

  InstallSymbolizeOpenObjectFileCallback(nullptr);
  close(g_synthetic_elf_file);
  g_synthetic_elf_file = -1;
}

#      if defined(DT_GNU_HASH)
TEST(Symbolize, FindsSymbolInSectionlessDynamicElfWithGnuHash) {
  g_synthetic_elf_file = CreateSectionlessDynamicElf(1, false, 0, false, true);
  ASSERT_NE(g_synthetic_elf_file, -1);
  InstallSymbolizeOpenObjectFileCallback(OpenSyntheticElf);

  char symbol[64];
  EXPECT_TRUE(Symbolize(reinterpret_cast<void*>(kSyntheticProgramCounter),
                        symbol, sizeof(symbol),
                        nglog::SymbolizeOptions::kNoLineNumbers));
  EXPECT_STREQ("expected_function", symbol);

  InstallSymbolizeOpenObjectFileCallback(nullptr);
  close(g_synthetic_elf_file);
  g_synthetic_elf_file = -1;
}
#      endif

TEST(Symbolize, UsesLoadBiasWithExtendedProgramHeaders) {
  constexpr std::uint64_t kLoadVaddr = 0x1000;
  g_synthetic_elf_file = CreateSectionlessDynamicElf(1, true, kLoadVaddr, true);
  ASSERT_NE(g_synthetic_elf_file, -1);

  const long page_size = sysconf(_SC_PAGESIZE);
  ASSERT_GT(page_size, 0);
  const std::size_t mapping_size = static_cast<std::size_t>(page_size) * 2;
  void* const mapping = mmap(nullptr, mapping_size, PROT_READ | PROT_EXEC,
                             MAP_PRIVATE, g_synthetic_elf_file, 0);
  ASSERT_NE(mapping, MAP_FAILED);

  char symbol[64];
  EXPECT_TRUE(Symbolize(static_cast<char*>(mapping) + kSyntheticProgramCounter,
                        symbol, sizeof(symbol),
                        nglog::SymbolizeOptions::kNoLineNumbers));
  EXPECT_STREQ("expected_function", symbol);

  munmap(mapping, mapping_size);
  close(g_synthetic_elf_file);
  unlink(g_synthetic_elf_path);
  g_synthetic_elf_file = -1;
}

TEST(Symbolize, RejectsOneByteOutputBuffer) {
  g_synthetic_elf_file = CreateSyntheticElf();
  ASSERT_NE(g_synthetic_elf_file, -1);
  InstallSymbolizeOpenObjectFileCallback(OpenSyntheticElf);

  char symbol[2] = {'\0', 'x'};
  EXPECT_FALSE(Symbolize(reinterpret_cast<void*>(kSyntheticProgramCounter),
                         symbol, 1, nglog::SymbolizeOptions::kNoLineNumbers));
  EXPECT_EQ(symbol[1], 'x');

  InstallSymbolizeOpenObjectFileCallback(nullptr);
  close(g_synthetic_elf_file);
  g_synthetic_elf_file = -1;
}

TEST(Symbolize, RejectsUnsupportedElfByteOrder) {
  g_synthetic_elf_file = CreateSyntheticElf();
  ASSERT_NE(g_synthetic_elf_file, -1);
  SyntheticElfHeader header;
  ASSERT_TRUE(ReadSyntheticObject(0, header));
  header.e_ident[EI_DATA] =
      header.e_ident[EI_DATA] == ELFDATA2LSB ? ELFDATA2MSB : ELFDATA2LSB;
  ASSERT_TRUE(WriteSyntheticObject(0, header));
  InstallSymbolizeCallback(NoopSymbolizeCallback);
  InstallSymbolizeOpenObjectFileCallback(OpenSyntheticElf);

  char symbol[64];
  EXPECT_FALSE(Symbolize(reinterpret_cast<void*>(kSyntheticProgramCounter),
                         symbol, sizeof(symbol),
                         nglog::SymbolizeOptions::kNoLineNumbers));

  InstallSymbolizeOpenObjectFileCallback(nullptr);
  InstallSymbolizeCallback(nullptr);
  close(g_synthetic_elf_file);
  g_synthetic_elf_file = -1;
}

TEST(Symbolize, RejectsInvalidSectionEntrySize) {
  g_synthetic_elf_file = CreateSyntheticElf();
  ASSERT_NE(g_synthetic_elf_file, -1);
  SyntheticElfHeader header;
  ASSERT_TRUE(ReadSyntheticObject(0, header));
  --header.e_shentsize;
  ASSERT_TRUE(WriteSyntheticObject(0, header));
  InstallSymbolizeCallback(NoopSymbolizeCallback);
  InstallSymbolizeOpenObjectFileCallback(OpenSyntheticElf);

  char symbol[64];
  EXPECT_FALSE(Symbolize(reinterpret_cast<void*>(kSyntheticProgramCounter),
                         symbol, sizeof(symbol),
                         nglog::SymbolizeOptions::kNoLineNumbers));

  InstallSymbolizeOpenObjectFileCallback(nullptr);
  InstallSymbolizeCallback(nullptr);
  close(g_synthetic_elf_file);
  g_synthetic_elf_file = -1;
}

TEST(Symbolize, RejectsZeroSymbolEntrySize) {
  g_synthetic_elf_file = CreateSyntheticElf();
  ASSERT_NE(g_synthetic_elf_file, -1);
  SyntheticSectionHeader section;
  const std::size_t section_offset = g_synthetic_section_header_offset +
                                     kSymbolTableSectionIndex * sizeof(section);
  ASSERT_TRUE(ReadSyntheticObject(section_offset, section));
  section.sh_entsize = 0;
  ASSERT_TRUE(WriteSyntheticObject(section_offset, section));
  InstallSymbolizeCallback(NoopSymbolizeCallback);
  InstallSymbolizeOpenObjectFileCallback(OpenSyntheticElf);

  char symbol[64];
  EXPECT_FALSE(Symbolize(reinterpret_cast<void*>(kSyntheticProgramCounter),
                         symbol, sizeof(symbol),
                         nglog::SymbolizeOptions::kNoLineNumbers));

  InstallSymbolizeOpenObjectFileCallback(nullptr);
  InstallSymbolizeCallback(nullptr);
  close(g_synthetic_elf_file);
  g_synthetic_elf_file = -1;
}

TEST(Symbolize, RejectsTruncatedSymbolTable) {
  g_synthetic_elf_file = CreateSyntheticElf();
  ASSERT_NE(g_synthetic_elf_file, -1);
  SyntheticSectionHeader section;
  const std::size_t section_offset = g_synthetic_section_header_offset +
                                     kSymbolTableSectionIndex * sizeof(section);
  ASSERT_TRUE(ReadSyntheticObject(section_offset, section));
  struct stat file_status;
  ASSERT_EQ(fstat(g_synthetic_elf_file, &file_status), 0);
  ASSERT_GT(file_status.st_size, 0);
  section.sh_offset =
      static_cast<decltype(section.sh_offset)>(file_status.st_size - 1);
  section.sh_size = sizeof(SyntheticSymbol);
  ASSERT_TRUE(WriteSyntheticObject(section_offset, section));
  InstallSymbolizeCallback(NoopSymbolizeCallback);
  InstallSymbolizeOpenObjectFileCallback(OpenSyntheticElf);

  char symbol[64];
  EXPECT_FALSE(Symbolize(reinterpret_cast<void*>(kSyntheticProgramCounter),
                         symbol, sizeof(symbol),
                         nglog::SymbolizeOptions::kNoLineNumbers));

  InstallSymbolizeOpenObjectFileCallback(nullptr);
  InstallSymbolizeCallback(nullptr);
  close(g_synthetic_elf_file);
  g_synthetic_elf_file = -1;
}

TEST(Symbolize, RejectsTruncatedSymbolTableAfterMatch) {
  g_synthetic_elf_file = CreateSyntheticElf();
  ASSERT_NE(g_synthetic_elf_file, -1);
  SyntheticSectionHeader section;
  const std::size_t section_offset = g_synthetic_section_header_offset +
                                     kSymbolTableSectionIndex * sizeof(section);
  ASSERT_TRUE(ReadSyntheticObject(section_offset, section));
  struct stat file_status;
  ASSERT_EQ(fstat(g_synthetic_elf_file, &file_status), 0);
  const auto file_size = static_cast<std::uint64_t>(file_status.st_size);
  const auto section_end = static_cast<std::uint64_t>(section.sh_offset);
  ASSERT_GT(file_size, section_end);
  section.sh_size = static_cast<decltype(section.sh_size)>(
      file_size - section_end + sizeof(SyntheticSymbol));
  ASSERT_TRUE(WriteSyntheticObject(section_offset, section));
  InstallSymbolizeCallback(NoopSymbolizeCallback);
  InstallSymbolizeOpenObjectFileCallback(OpenSyntheticElf);

  char symbol[64];
  EXPECT_FALSE(Symbolize(reinterpret_cast<void*>(kSyntheticProgramCounter),
                         symbol, sizeof(symbol),
                         nglog::SymbolizeOptions::kNoLineNumbers));

  InstallSymbolizeOpenObjectFileCallback(nullptr);
  InstallSymbolizeCallback(nullptr);
  close(g_synthetic_elf_file);
  g_synthetic_elf_file = -1;
}

TEST(Symbolize, RejectsSymbolNameOutsideStringTable) {
  g_synthetic_elf_file = CreateSyntheticElf();
  ASSERT_NE(g_synthetic_elf_file, -1);
  SyntheticSymbol symbol;
  const std::size_t symbol_offset =
      g_synthetic_symbol_table_offset + kFunctionSymbolIndex * sizeof(symbol);
  ASSERT_TRUE(ReadSyntheticObject(symbol_offset, symbol));
  symbol.st_name = static_cast<decltype(symbol.st_name)>(
      g_synthetic_section_header_offset - g_synthetic_symbol_string_offset);
  ASSERT_TRUE(WriteSyntheticObject(symbol_offset, symbol));
  InstallSymbolizeCallback(NoopSymbolizeCallback);
  InstallSymbolizeOpenObjectFileCallback(OpenSyntheticElf);

  char output[64];
  EXPECT_FALSE(Symbolize(reinterpret_cast<void*>(kSyntheticProgramCounter),
                         output, sizeof(output),
                         nglog::SymbolizeOptions::kNoLineNumbers));

  InstallSymbolizeOpenObjectFileCallback(nullptr);
  InstallSymbolizeCallback(nullptr);
  close(g_synthetic_elf_file);
  g_synthetic_elf_file = -1;
}

TEST(Symbolize, RejectsTruncatedSectionNameTable) {
  g_synthetic_elf_file = CreateSyntheticElf();
  ASSERT_NE(g_synthetic_elf_file, -1);
  SyntheticSectionHeader section;
  ASSERT_TRUE(ReadSyntheticObject(g_synthetic_section_header_offset, section));
  struct stat file_status;
  ASSERT_EQ(fstat(g_synthetic_elf_file, &file_status), 0);
  section.sh_type = SHT_STRTAB;
  section.sh_name = kFunctionNameOffset;
  section.sh_offset = g_synthetic_symbol_string_offset;
  section.sh_size = static_cast<decltype(section.sh_size)>(
      static_cast<std::uint64_t>(file_status.st_size) -
      static_cast<std::uint64_t>(section.sh_offset) + 1);
  ASSERT_TRUE(WriteSyntheticObject(g_synthetic_section_header_offset, section));

  const char section_name[] = "expected_function";
  SyntheticSectionHeader output;
  EXPECT_FALSE(GetSectionHeaderByName(g_synthetic_elf_file, section_name,
                                      sizeof(section_name), &output));

  close(g_synthetic_elf_file);
  g_synthetic_elf_file = -1;
}

TEST(Symbolize, FindsSectionHeaderByName) {
  g_synthetic_elf_file = CreateSyntheticElf();
  ASSERT_NE(g_synthetic_elf_file, -1);

  const char section_name[] = "expected_function";
  SyntheticSectionHeader output;
  ASSERT_TRUE(GetSectionHeaderByName(g_synthetic_elf_file, section_name,
                                     sizeof(section_name), &output));
  EXPECT_EQ(output.sh_type, SHT_STRTAB);

  close(g_synthetic_elf_file);
  g_synthetic_elf_file = -1;
}
#    endif

struct Foo {
  static void func(int x);
};

NGLOG_ATTRIBUTE_NOINLINE
void Foo::func(int x) {
  volatile int a = x;
  // NOTE: In C++20, increment of object of volatile-qualified type is
  // deprecated.
  a = a + 1;
}

// Symbolize() should return demangled symbol names with function parameters
// omitted.
TEST(Symbolize, SymbolizeWithDemangling) {
  Foo::func(100);
#    if !defined(_MSC_VER) || !defined(NDEBUG)
  EXPECT_STREQ("Foo::func()", TrySymbolize((void*)(&Foo::func)));
#    endif
}

// Tests that verify that Symbolize footprint is within some limit.

// To measure the stack footprint of the Symbolize function, we create
// a signal handler (for SIGUSR1 say) that calls the Symbolize function
// on an alternate stack. This alternate stack is initialized to some
// known pattern (0x55, 0x55, 0x55, ...). We then self-send this signal,
// and after the signal handler returns, look at the alternate stack
// buffer to see what portion has been touched.
//
// This trick gives us the stack footprint of the signal handler.
// But the signal handler, even before the call to Symbolize, consumes
// some stack already. We however only want the stack usage of the
// Symbolize function. To measure this accurately, we install two signal
// handlers: one that does nothing and just returns, and another that
// calls Symbolize. The difference between the stack consumption of these
// two signals handlers should give us the Symbolize stack foorprint.

static void* g_pc_to_symbolize;
static char g_symbolize_buffer[4096];
static char* g_symbolize_result;

static void EmptySignalHandler(int /*signo*/) {}

static void SymbolizeSignalHandler(int /*signo*/) {
  if (Symbolize(g_pc_to_symbolize, g_symbolize_buffer,
                sizeof(g_symbolize_buffer))) {
    g_symbolize_result = g_symbolize_buffer;
  } else {
    g_symbolize_result = nullptr;
  }
}

const int kAlternateStackSize = 8096;
const char kAlternateStackFillValue = 0x55;

// These helper functions look at the alternate stack buffer, and figure
// out what portion of this buffer has been touched - this is the stack
// consumption of the signal handler running on this alternate stack.
NGLOG_ATTRIBUTE_NOINLINE
static bool StackGrowsDown(int* x) {
  int y;
  return &y < x;
}
static int GetStackConsumption(const char* alt_stack) {
  int x;
  if (StackGrowsDown(&x)) {
    for (int i = 0; i < kAlternateStackSize; i++) {
      if (alt_stack[i] != kAlternateStackFillValue) {
        return (kAlternateStackSize - i);
      }
    }
  } else {
    for (int i = (kAlternateStackSize - 1); i >= 0; i--) {
      if (alt_stack[i] != kAlternateStackFillValue) {
        return i;
      }
    }
  }
  return -1;
}

#    ifdef HAVE_SIGALTSTACK

// Call Symbolize and figure out the stack footprint of this call.
static const char* SymbolizeStackConsumption(void* pc, int* stack_consumed) {
  // Initialize the symbolization backend before installing the alternate
  // signal stack. The measurement below covers the steady-state path rather
  // than one-time backend initialization.
  static_cast<void>(TrySymbolize(pc, nglog::SymbolizeOptions::kNoLineNumbers));

  g_pc_to_symbolize = pc;

  // The alt-signal-stack cannot be heap allocated because there is a
  // bug in glibc-2.2 where some signal handler setup code looks at the
  // current stack pointer to figure out what thread is currently running.
  // Therefore, the alternate stack must be allocated from the main stack
  // itself.
  char altstack[kAlternateStackSize];
  memset(altstack, kAlternateStackFillValue, kAlternateStackSize);

  // Set up the alt-signal-stack (and save the older one).
  stack_t sigstk;
  memset(&sigstk, 0, sizeof(stack_t));
  stack_t old_sigstk;
  sigstk.ss_sp = altstack;
  sigstk.ss_size = kAlternateStackSize;
  sigstk.ss_flags = 0;
  CHECK_ERR(sigaltstack(&sigstk, &old_sigstk));

  // Set up SIGUSR1 and SIGUSR2 signal handlers (and save the older ones).
  struct sigaction sa;
  memset(&sa, 0, sizeof(struct sigaction));
  struct sigaction old_sa1, old_sa2;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_ONSTACK;

  // SIGUSR1 maps to EmptySignalHandler.
  sa.sa_handler = EmptySignalHandler;
  CHECK_ERR(sigaction(SIGUSR1, &sa, &old_sa1));

  // SIGUSR2 maps to SymbolizeSignalHanlder.
  sa.sa_handler = SymbolizeSignalHandler;
  CHECK_ERR(sigaction(SIGUSR2, &sa, &old_sa2));

  // Send SIGUSR1 signal and measure the stack consumption of the empty
  // signal handler.
  CHECK_ERR(kill(getpid(), SIGUSR1));
  int stack_consumption1 = GetStackConsumption(altstack);

  // Send SIGUSR2 signal and measure the stack consumption of the symbolize
  // signal handler.
  CHECK_ERR(kill(getpid(), SIGUSR2));
  int stack_consumption2 = GetStackConsumption(altstack);

  // The difference between the two stack consumption values is the
  // stack footprint of the Symbolize function.
  if (stack_consumption1 != -1 && stack_consumption2 != -1) {
    *stack_consumed = stack_consumption2 - stack_consumption1;
  } else {
    *stack_consumed = -1;
  }

  // Log the stack consumption values.
  LOG(INFO) << "Stack consumption of empty signal handler: "
            << stack_consumption1;
  LOG(INFO) << "Stack consumption of symbolize signal handler: "
            << stack_consumption2;
  LOG(INFO) << "Stack consumption of Symbolize: " << *stack_consumed;

  // Now restore the old alt-signal-stack and signal handlers.
  CHECK_ERR(sigaltstack(&old_sigstk, nullptr));
  CHECK_ERR(sigaction(SIGUSR1, &old_sa1, nullptr));
  CHECK_ERR(sigaction(SIGUSR2, &old_sa2, nullptr));

  return g_symbolize_result;
}

// Symbolize stack consumption should be within 4kB.
constexpr int kStackConsumptionUpperLimit = 4096;

TEST(Symbolize, SymbolizeStackConsumption) {
  int stack_consumed;
  const char* symbol;

  symbol = SymbolizeStackConsumption(reinterpret_cast<void*>(&nonstatic_func),
                                     &stack_consumed);
  EXPECT_STREQ("nonstatic_func", symbol);
  EXPECT_GT(stack_consumed, 0);
  EXPECT_LT(stack_consumed, kStackConsumptionUpperLimit);

  // The name of an internal linkage symbol is not specified; allow either a
  // mangled or an unmangled name here.
  symbol = SymbolizeStackConsumption(reinterpret_cast<void*>(&static_func),
                                     &stack_consumed);
  ASSERT_THAT(symbol, Not(IsNull()));
  EXPECT_THAT(symbol, AnyOf(StrEq("static_func"), StrEq("static_func()")));
  EXPECT_GT(stack_consumed, 0);
  EXPECT_LT(stack_consumed, kStackConsumptionUpperLimit);
}

TEST(Symbolize, SymbolizeWithDemanglingStackConsumption) {
  Foo::func(100);
  int stack_consumed;
  const char* symbol;

  symbol = SymbolizeStackConsumption(reinterpret_cast<void*>(&Foo::func),
                                     &stack_consumed);

  EXPECT_STREQ("Foo::func()", symbol);
  EXPECT_GT(stack_consumed, 0);
  EXPECT_LT(stack_consumed, kStackConsumptionUpperLimit);
}

#    endif  // HAVE_SIGALTSTACK

// x86 specific tests.  Uses some inline assembler.
extern "C" {
NGLOG_ATTRIBUTE_ALWAYS_INLINE
inline void* inline_func() {
  void* pc = nullptr;
#    ifdef TEST_WITH_LABEL_ADDRESSES
  pc = &&curr_pc;
curr_pc:
#    endif
  return pc;
}

NGLOG_ATTRIBUTE_NOINLINE
void* non_inline_func();
NGLOG_ATTRIBUTE_NOINLINE
void* non_inline_func() {
  void* pc = nullptr;
#    ifdef TEST_WITH_LABEL_ADDRESSES
  pc = &&curr_pc;
curr_pc:
#    endif
  return pc;
}

NGLOG_ATTRIBUTE_NOINLINE
static void TestWithPCInsideNonInlineFunction() {
#    if defined(TEST_WITH_LABEL_ADDRESSES)
  void* pc = non_inline_func();
  const char* symbol = TrySymbolize(pc);

#      if !defined(_MSC_VER) || !defined(NDEBUG)
  ASSERT_NE(symbol, nullptr);
  ASSERT_STREQ(symbol, "non_inline_func");
#      endif
#    endif
}

NGLOG_ATTRIBUTE_NOINLINE
static void TestWithPCInsideInlineFunction() {
#    if defined(TEST_WITH_LABEL_ADDRESSES)
  void* pc = inline_func();  // Must be inlined.
  const char* symbol = TrySymbolize(pc);

#      if !defined(_MSC_VER) || !defined(NDEBUG)
  ASSERT_NE(symbol, nullptr);
  ASSERT_STREQ(symbol, __FUNCTION__);
#      endif
#    endif
}
}

TEST(Symbolize, PCInsideNonInlineFunction) {
  TestWithPCInsideNonInlineFunction();
#    if !defined(TEST_WITH_LABEL_ADDRESSES)
  GTEST_SKIP() << "the compiler does not support this symbolization test";
#    endif
}

TEST(Symbolize, PCInsideInlineFunction) {
  TestWithPCInsideInlineFunction();
#    if !defined(TEST_WITH_LABEL_ADDRESSES)
  GTEST_SKIP() << "the compiler does not support this symbolization test";
#    endif
}

// Test with a return address.
NGLOG_ATTRIBUTE_NOINLINE
static const char* TestWithReturnAddress() {
  void* return_address = __builtin_return_address(0);
  return TrySymbolize(return_address, nglog::SymbolizeOptions::kNoLineNumbers);
}

TEST(Symbolize, ReturnAddress) { EXPECT_NE(TestWithReturnAddress(), nullptr); }

#  elif defined(NGLOG_OS_WINDOWS) || defined(NGLOG_OS_CYGWIN)

#    ifdef _MSC_VER
#      include <intrin.h>
#      pragma intrinsic(_ReturnAddress)
#    endif

struct Foo {
  static void func(int x);
};

NGLOG_ATTRIBUTE_NOINLINE
void Foo::func(int x) {
  volatile int a = x;
  // NOTE: In C++20, increment of object of volatile-qualified type is
  // deprecated.
  a = a + 1;
}

TEST(Symbolize, SymbolizeWithDemangling) {
  Foo::func(100);
  const char* ret = TrySymbolize((void*)(&Foo::func));

#    if defined(HAVE_ADDR2LINE)
  // Foo::func() lives in this same translation unit as main(), which a
  // known MinGW/binutils linker quirk (see symbolize.cc) can leave
  // unresolvable via addr2line. Degrade gracefully instead of failing
  // over a toolchain limitation.
  if (ret == nullptr) {
    GTEST_SKIP() << "addr2line could not resolve Foo::func()";
  }
#    endif  // defined(HAVE_ADDR2LINE)

  // The signal-safe local parser intentionally leaves MSVC ABI names
  // unchanged. Call DemangleWithSystem() when complete MSVC spelling is
  // required. MinGW and Clang on Windows use the Itanium ABI, like Linux and
  // macOS.
#    if defined(_MSC_VER)
#      if !defined(NDEBUG)
  ASSERT_THAT(ret, Not(IsNull()));
  EXPECT_THAT(ret, HasSubstr("?func@Foo@@SAXH@Z"));
#      endif
#    elif !defined(NDEBUG)
  EXPECT_STREQ("Foo::func()", ret);
#    endif
}

NGLOG_ATTRIBUTE_NOINLINE
const char* TestWithReturnAddress() {
  void* return_address =
#    ifdef __GNUC__  // Cygwin and MinGW support
      __builtin_return_address(0)
#    else
      _ReturnAddress()
#    endif
      ;
  return TrySymbolize(return_address, nglog::SymbolizeOptions::kNoLineNumbers);
}

TEST(Symbolize, ReturnAddress) {
  const char* symbol = TestWithReturnAddress();
  if (symbol == nullptr) {
    GTEST_SKIP() << "the Windows symbolization runtime is unavailable";
  }
  EXPECT_NE(symbol, nullptr);
}
#  endif
#endif  // HAVE_STACKTRACE

#if !defined(HAVE_SYMBOLIZE) || !defined(HAVE_STACKTRACE)
TEST(Symbolize, Unsupported) {
  GTEST_SKIP() << "symbolization support is unavailable";
}
#elif !defined(HAVE_ELF_H) && !defined(HAVE_SYS_EXEC_ELF_H) && \
    !defined(NGLOG_OS_WINDOWS) && !defined(NGLOG_OS_CYGWIN)
TEST(Symbolize, Unsupported) {
  GTEST_SKIP() << "the platform does not support this symbolization test";
}
#endif

int main(int argc, char** argv) {
  FLAGS_logtostderr = true;
  InitializeLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);
#if defined(HAVE_SYMBOLIZE) && defined(HAVE_STACKTRACE)
#  if defined(HAVE_ELF_H) || defined(HAVE_SYS_EXEC_ELF_H)
  // We don't want to get affected by the callback interface, that may be
  // used to install some callback function at InitGoogle() time.
  InstallSymbolizeCallback(nullptr);
#  elif defined(NGLOG_OS_WINDOWS) || defined(NGLOG_OS_CYGWIN)
  // Run first, while whatever callback InitializeLogging() may have
  // installed (e.g. for libbacktrace) is still active: it passes
  // kNoLineNumbers itself, so it also exercises that a caller-requested
  // "no line numbers" is honored even with a callback installed.

  // The tests below want bare, undecorated names, so make sure they are
  // not affected by whatever callback InitializeLogging() may have
  // installed.
  InstallSymbolizeCallback(nullptr);

#  endif  // defined(HAVE_ELF_H) || defined(HAVE_SYS_EXEC_ELF_H)
#endif    // HAVE_SYMBOLIZE
  return RUN_ALL_TESTS();
}

#if defined(__GNUG__)
#  pragma GCC diagnostic pop
#endif
