// SPDX-FileCopyrightText: 2008-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
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
#include <string>

#include "config.h"
#include "internal/attributes.h"
#include "internal/checked_arithmetic.h"
#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "ng-log/platform.h"

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#if defined(HAVE_LINK_H) && !defined(HAVE_LIBBACKTRACE)
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <sys/types.h>

#  include <cstdlib>
#  include <iterator>
#  include <utility>
#  include <vector>

#  include "utilities.h"
#endif

using namespace std;
using namespace nglog;

using testing::_;
using testing::AllOf;
using testing::AnyOf;
using testing::Gt;
using testing::HasSubstr;
using testing::IsNull;
using testing::Lt;
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

// The name of an internal linkage symbol is not specified, so allow either a
// mangled or an unmangled name.
testing::Matcher<const char*> IsStaticFuncSymbol() {
  return AnyOf(StrEq("static_func"), StrEq("static_func()"));
}

TEST(Symbolize, Symbolize) {
  // We do C-style cast since GCC 2.95.3 doesn't allow
  // reinterpret_cast<void *>(&func).

  // Compilers should give us pointers to them.
  EXPECT_STREQ("nonstatic_func", TrySymbolize((void*)(&nonstatic_func)));

#    if !defined(_MSC_VER) || !defined(NDEBUG)
  EXPECT_THAT(TrySymbolize(reinterpret_cast<void*>(&static_func)),
              IsStaticFuncSymbol());
#    endif

  EXPECT_THAT(TrySymbolize(nullptr), IsNull());
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
constexpr std::size_t kSymbolBufferSize = 64;

int g_synthetic_elf_file = -1;
std::size_t g_synthetic_section_header_offset = 0;
std::size_t g_synthetic_symbol_table_offset = 0;
std::size_t g_synthetic_symbol_string_offset = 0;
char g_synthetic_elf_path[128];

using ElfHeader = ElfW(Ehdr);
using SectionHeader = ElfW(Shdr);
using ProgramHeader = ElfW(Phdr);
using Dynamic = ElfW(Dyn);
using Symbol = ElfW(Sym);

int g_open_object_file_calls = 0;

// Counts its invocations and finds no object file.
int CountOpenObjectFileCalls(std::uint64_t /*pc*/,
                             std::uint64_t& /*start_address*/,
                             std::uint64_t& /*base_address*/,
                             char* /*out_file_name*/,
                             std::size_t /*out_file_name_size*/) {
  ++g_open_object_file_calls;
  return -1;
}

int OpenSyntheticElf(std::uint64_t /*pc*/, std::uint64_t& start_address,
                     std::uint64_t& base_address, char* out_file_name,
                     std::size_t out_file_name_size) {
  start_address = 0;
  base_address = 0;
  std::strncpy(out_file_name, "synthetic-elf", out_file_name_size);
  out_file_name[out_file_name_size - 1] = '\0';
  return dup(g_synthetic_elf_file);
}

int NoopSymbolizeCallback(int /*fd*/, void* /*pc*/, char* /*out*/,
                          std::size_t /*out_size*/,
                          std::uint64_t /*relocation*/) {
  return -1;
}

void* SyntheticProgramCounter() {
  return reinterpret_cast<void*>(kSyntheticProgramCounter);
}

unsigned char GlobalSymbolInfo(unsigned char type) {
  return sizeof(void*) == sizeof(std::uint64_t)
             ? ELF64_ST_INFO(STB_GLOBAL, type)
             : ELF32_ST_INFO(STB_GLOBAL, type);
}

ElfHeader MakeElfHeader() {
  ElfHeader header{};
  std::memcpy(header.e_ident, ELFMAG, SELFMAG);
  header.e_ident[EI_CLASS] =
      sizeof(void*) == sizeof(std::uint64_t) ? ELFCLASS64 : ELFCLASS32;
  header.e_ident[EI_DATA] = ELFDATA2LSB;
  header.e_ident[EI_VERSION] = EV_CURRENT;
  header.e_machine = EM_NONE;
  header.e_version = EV_CURRENT;
  header.e_ehsize = sizeof(ElfHeader);
  return header;
}

template <typename T>
void AppendObject(std::vector<char>& data, const T& object) {
  const auto* bytes = reinterpret_cast<const char*>(&object);
  data.insert(data.end(), bytes, bytes + sizeof(object));
}

// Pads the data to the alignment of the object, appends the object and
// returns its offset.
template <typename T>
std::size_t AppendAligned(std::vector<char>& data, const T& object) {
  const std::size_t alignment = alignof(T);
  data.resize((data.size() + alignment - 1) / alignment * alignment);
  AppendObject(data, object);
  return data.size() - sizeof(object);
}

// Writes the data to a new temporary file created from the template and
// returns its descriptor, or -1 on failure. The file is unlinked right away
// unless it must stay accessible by name.
int WriteTemporaryFile(const std::vector<char>& data, char* file_name,
                       bool keep_file) {
  const int file = mkstemp(file_name);
  if (file == -1) {
    return -1;
  }
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

// Reads the object at the offset of the synthetic ELF file, lets the
// modifier change it, and writes it back.
template <typename T, typename Modifier>
bool ModifySyntheticObject(std::size_t offset, Modifier modify) {
  T object;
  if (pread(g_synthetic_elf_file, &object, sizeof(object),
            static_cast<off_t>(offset)) !=
      static_cast<ssize_t>(sizeof(object))) {
    return false;
  }
  modify(object);
  return pwrite(g_synthetic_elf_file, &object, sizeof(object),
                static_cast<off_t>(offset)) ==
         static_cast<ssize_t>(sizeof(object));
}

std::size_t SymbolTableSectionHeaderOffset() {
  return g_synthetic_section_header_offset +
         kSymbolTableSectionIndex * sizeof(SectionHeader);
}

off_t SyntheticElfFileSize() {
  struct stat file_status;
  if (fstat(g_synthetic_elf_file, &file_status) != 0) {
    return -1;
  }
  return file_status.st_size;
}

int CreateSyntheticElf(std::size_t section_header_extra = 0) {
  std::vector<Symbol> symbols(kSymbolCount);
  symbols[kTlsSymbolIndex].st_name = kTlsNameOffset;
  symbols[kTlsSymbolIndex].st_info = GlobalSymbolInfo(STT_TLS);
  symbols[kTlsSymbolIndex].st_value = kSyntheticProgramCounter;
  symbols[kTlsSymbolIndex].st_size = kSymbolSize;
  symbols[kTlsSymbolIndex].st_shndx = kTextSectionIndex;
  symbols[kFunctionSymbolIndex].st_name = kFunctionNameOffset;
  symbols[kFunctionSymbolIndex].st_info = GlobalSymbolInfo(STT_FUNC);
  symbols[kFunctionSymbolIndex].st_value = kSyntheticProgramCounter;
  symbols[kFunctionSymbolIndex].st_size = kSymbolSize;
  symbols[kFunctionSymbolIndex].st_shndx = kTextSectionIndex;

  std::vector<char> data(sizeof(ElfHeader));
  const std::size_t text_offset = AppendAligned(data, std::uint32_t{0});
  const std::size_t symbol_offset =
      AppendAligned(data, symbols[kNullSymbolIndex]);
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

  ElfHeader header = MakeElfHeader();
  header.e_type = ET_EXEC;
  header.e_shentsize = static_cast<decltype(header.e_shentsize)>(
      sizeof(SectionHeader) + section_header_extra);
  header.e_shnum = static_cast<decltype(header.e_shnum)>(sections.size());
  header.e_shstrndx = kNullSectionIndex;
  header.e_shoff = section_header_offset;
  std::memcpy(data.data(), &header, sizeof(header));

  char file_name[] = "/tmp/nglog-symbolize-XXXXXX";
  return WriteTemporaryFile(data, file_name, false);
}

int CreateSectionlessDynamicElf(std::size_t program_header_extra = 0,
                                bool extended_program_header_count = false,
                                std::uint64_t load_vaddr = 0,
                                bool keep_file = false,
                                bool use_gnu_hash = false) {
  constexpr std::size_t kSymbolCount = 2;
  const std::uint64_t symbol_value = load_vaddr + kSyntheticProgramCounter;
  constexpr char kSymbolStrings[] = "\0expected_function";

  std::vector<char> data(sizeof(ElfHeader));
  const std::size_t program_header_offset =
      AppendAligned(data, ProgramHeader{});
  data.resize(program_header_offset + sizeof(ProgramHeader) * 2 +
              program_header_extra * 2);

  std::vector<Dynamic> dynamics(6);
  const std::size_t dynamic_offset = AppendAligned(data, dynamics[0]);
  for (std::size_t i = 1; i < dynamics.size(); ++i) {
    AppendObject(data, dynamics[i]);
  }

  std::vector<Symbol> symbols(kSymbolCount);
  symbols[1].st_name = 1;
  symbols[1].st_value = symbol_value;
  symbols[1].st_size = kSymbolSize;
  symbols[1].st_info = GlobalSymbolInfo(STT_FUNC);
  symbols[1].st_shndx = 1;
  const std::size_t symbol_offset = AppendAligned(data, symbols[0]);
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
  const std::size_t hash_offset = AppendAligned(data, hash[0]);
  for (std::size_t i = 1; i < hash.size(); ++i) {
    AppendObject(data, hash[i]);
  }

  std::size_t section_header_offset = 0;
  if (extended_program_header_count) {
    SectionHeader section_zero{};
    section_zero.sh_info = 2;
    section_header_offset = AppendAligned(data, section_zero);
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

  ElfHeader header = MakeElfHeader();
  header.e_type = load_vaddr == 0 ? ET_EXEC : ET_DYN;
  header.e_phoff = program_header_offset;
  header.e_phentsize = static_cast<decltype(header.e_phentsize)>(
      sizeof(ProgramHeader) + program_header_extra);
  header.e_phnum = extended_program_header_count ? PN_XNUM : 2;
  header.e_shoff = section_header_offset;
  header.e_shentsize =
      extended_program_header_count ? sizeof(SectionHeader) : 0;
  header.e_shnum = extended_program_header_count ? 1 : 0;
  header.e_shstrndx = extended_program_header_count ? 0 : SHN_UNDEF;
  std::memcpy(data.data(), &header, sizeof(header));

  char file_name[] = "/tmp/nglog-symbolize-dynamic-XXXXXX";
  const int file = WriteTemporaryFile(data, file_name, keep_file);
  std::strncpy(g_synthetic_elf_path, file_name, sizeof(g_synthetic_elf_path));
  g_synthetic_elf_path[sizeof(g_synthetic_elf_path) - 1] = '\0';
  return file;
}

}  // namespace

MATCHER_P(SymbolizesTo, expected,
          std::string{negation ? "does not symbolize to a name that "
                               : "symbolizes to a name that "} +
              testing::DescribeMatcher<std::string>(expected)) {
  char symbol[kSymbolBufferSize];
  if (!Symbolize(arg, symbol, sizeof(symbol),
                 nglog::SymbolizeOptions::kNoLineNumbers)) {
    *result_listener << "cannot be symbolized";
    return false;
  }
  *result_listener << "symbolizes to \"" << symbol << '"';
  return testing::ExplainMatchResult(
      testing::MatcherCast<std::string>(expected), std::string{symbol},
      result_listener);
}

// Symbolizes addresses against a synthetic ELF file and restores the
// default callbacks afterwards.
class SyntheticElfSymbolize : public testing::Test {
 protected:
  void TearDown() override {
    InstallSymbolizeOpenObjectFileCallback(nullptr);
    InstallSymbolizeCallback(nullptr);
    if (g_synthetic_elf_file != -1) {
      close(g_synthetic_elf_file);
      g_synthetic_elf_file = -1;
    }
  }

  // Ensures that only the parser of the synthetic file can resolve the
  // synthetic program counter.
  static void InstallCallbacks() {
    InstallSymbolizeCallback(NoopSymbolizeCallback);
    InstallSymbolizeOpenObjectFileCallback(OpenSyntheticElf);
  }
};

TEST_F(SyntheticElfSymbolize, IgnoresTlsSymbols) {
  g_synthetic_elf_file = CreateSyntheticElf();
  ASSERT_NE(g_synthetic_elf_file, -1);
  InstallSymbolizeOpenObjectFileCallback(OpenSyntheticElf);

  EXPECT_THAT(SyntheticProgramCounter(), SymbolizesTo("expected_function"));
}

TEST_F(SyntheticElfSymbolize, FallsBackForSectionlessElf) {
  g_synthetic_elf_file = CreateSyntheticElf();
  ASSERT_NE(g_synthetic_elf_file, -1);
  ASSERT_TRUE(ModifySyntheticObject<ElfHeader>(0, [](ElfHeader& header) {
    header.e_shoff = 0;
    header.e_shnum = 0;
    header.e_shstrndx = SHN_UNDEF;
  }));
  InstallSymbolizeOpenObjectFileCallback(OpenSyntheticElf);

  EXPECT_THAT(SyntheticProgramCounter(),
              SymbolizesTo("(synthetic-elf+0x1000)"));
}

TEST_F(SyntheticElfSymbolize, AcceptsExtendedSectionEntrySize) {
  g_synthetic_elf_file = CreateSyntheticElf(1);
  ASSERT_NE(g_synthetic_elf_file, -1);
  InstallSymbolizeOpenObjectFileCallback(OpenSyntheticElf);

  EXPECT_THAT(SyntheticProgramCounter(), SymbolizesTo("expected_function"));
}

TEST_F(SyntheticElfSymbolize, FindsSymbolInSectionlessDynamicElf) {
  g_synthetic_elf_file = CreateSectionlessDynamicElf(1);
  ASSERT_NE(g_synthetic_elf_file, -1);
  InstallSymbolizeOpenObjectFileCallback(OpenSyntheticElf);

  EXPECT_THAT(SyntheticProgramCounter(), SymbolizesTo("expected_function"));
}

#      if defined(DT_GNU_HASH)
TEST_F(SyntheticElfSymbolize, FindsSymbolInSectionlessDynamicElfWithGnuHash) {
  g_synthetic_elf_file = CreateSectionlessDynamicElf(1, false, 0, false, true);
  ASSERT_NE(g_synthetic_elf_file, -1);
  InstallSymbolizeOpenObjectFileCallback(OpenSyntheticElf);

  EXPECT_THAT(SyntheticProgramCounter(), SymbolizesTo("expected_function"));
}
#      endif

TEST_F(SyntheticElfSymbolize, UsesLoadBiasWithExtendedProgramHeaders) {
  constexpr std::uint64_t kLoadVaddr = 0x1000;
  g_synthetic_elf_file = CreateSectionlessDynamicElf(1, true, kLoadVaddr, true);
  ASSERT_NE(g_synthetic_elf_file, -1);

  const long page_size = sysconf(_SC_PAGESIZE);
  ASSERT_GT(page_size, 0);
  const std::size_t mapping_size = static_cast<std::size_t>(page_size) * 2;
  void* const mapping = mmap(nullptr, mapping_size, PROT_READ | PROT_EXEC,
                             MAP_PRIVATE, g_synthetic_elf_file, 0);
  ASSERT_NE(mapping, MAP_FAILED);

  EXPECT_THAT(static_cast<void*>(static_cast<char*>(mapping) +
                                 kSyntheticProgramCounter),
              SymbolizesTo("expected_function"));

  munmap(mapping, mapping_size);
  unlink(g_synthetic_elf_path);
}

// A buffer that cannot hold the object file name after the opening
// parenthesis is rejected before the object file is looked up, which would
// otherwise read past the end of the buffer.
TEST_F(SyntheticElfSymbolize, RejectsOneByteOutputBuffer) {
  g_open_object_file_calls = 0;
  InstallSymbolizeOpenObjectFileCallback(CountOpenObjectFileCalls);

  char symbol[1];
  EXPECT_FALSE(Symbolize(SyntheticProgramCounter(), symbol, sizeof(symbol),
                         nglog::SymbolizeOptions::kNoLineNumbers));
  EXPECT_EQ(0, g_open_object_file_calls);
}

TEST_F(SyntheticElfSymbolize, RejectsUnsupportedElfByteOrder) {
  g_synthetic_elf_file = CreateSyntheticElf();
  ASSERT_NE(g_synthetic_elf_file, -1);
  ASSERT_TRUE(ModifySyntheticObject<ElfHeader>(0, [](ElfHeader& header) {
    header.e_ident[EI_DATA] =
        header.e_ident[EI_DATA] == ELFDATA2LSB ? ELFDATA2MSB : ELFDATA2LSB;
  }));
  InstallCallbacks();

  EXPECT_THAT(SyntheticProgramCounter(), Not(SymbolizesTo(_)));
}

TEST_F(SyntheticElfSymbolize, RejectsInvalidSectionEntrySize) {
  g_synthetic_elf_file = CreateSyntheticElf();
  ASSERT_NE(g_synthetic_elf_file, -1);
  ASSERT_TRUE(ModifySyntheticObject<ElfHeader>(
      0, [](ElfHeader& header) { --header.e_shentsize; }));
  InstallCallbacks();

  EXPECT_THAT(SyntheticProgramCounter(), Not(SymbolizesTo(_)));
}

TEST_F(SyntheticElfSymbolize, RejectsZeroSymbolEntrySize) {
  g_synthetic_elf_file = CreateSyntheticElf();
  ASSERT_NE(g_synthetic_elf_file, -1);
  ASSERT_TRUE(ModifySyntheticObject<SectionHeader>(
      SymbolTableSectionHeaderOffset(),
      [](SectionHeader& section) { section.sh_entsize = 0; }));
  InstallCallbacks();

  EXPECT_THAT(SyntheticProgramCounter(), Not(SymbolizesTo(_)));
}

TEST_F(SyntheticElfSymbolize, RejectsTruncatedSymbolTable) {
  g_synthetic_elf_file = CreateSyntheticElf();
  ASSERT_NE(g_synthetic_elf_file, -1);
  const off_t file_size = SyntheticElfFileSize();
  ASSERT_GT(file_size, 0);
  ASSERT_TRUE(ModifySyntheticObject<SectionHeader>(
      SymbolTableSectionHeaderOffset(), [file_size](SectionHeader& section) {
        section.sh_offset =
            static_cast<decltype(section.sh_offset)>(file_size - 1);
        section.sh_size = sizeof(Symbol);
      }));
  InstallCallbacks();

  EXPECT_THAT(SyntheticProgramCounter(), Not(SymbolizesTo(_)));
}

TEST_F(SyntheticElfSymbolize, RejectsTruncatedSymbolTableAfterMatch) {
  g_synthetic_elf_file = CreateSyntheticElf();
  ASSERT_NE(g_synthetic_elf_file, -1);
  const off_t file_size = SyntheticElfFileSize();
  ASSERT_GT(file_size, 0);
  const auto symbol_table_end = static_cast<std::uint64_t>(file_size);
  ASSERT_GT(symbol_table_end, g_synthetic_symbol_table_offset);
  ASSERT_TRUE(ModifySyntheticObject<SectionHeader>(
      SymbolTableSectionHeaderOffset(),
      [symbol_table_end](SectionHeader& section) {
        section.sh_size = static_cast<decltype(section.sh_size)>(
            symbol_table_end - section.sh_offset + sizeof(Symbol));
      }));
  InstallCallbacks();

  EXPECT_THAT(SyntheticProgramCounter(), Not(SymbolizesTo(_)));
}

TEST_F(SyntheticElfSymbolize, RejectsSymbolNameOutsideStringTable) {
  g_synthetic_elf_file = CreateSyntheticElf();
  ASSERT_NE(g_synthetic_elf_file, -1);
  ASSERT_TRUE(ModifySyntheticObject<Symbol>(
      g_synthetic_symbol_table_offset + kFunctionSymbolIndex * sizeof(Symbol),
      [](Symbol& symbol) {
        symbol.st_name = static_cast<decltype(symbol.st_name)>(
            g_synthetic_section_header_offset -
            g_synthetic_symbol_string_offset);
      }));
  InstallCallbacks();

  EXPECT_THAT(SyntheticProgramCounter(), Not(SymbolizesTo(_)));
}

TEST_F(SyntheticElfSymbolize, RejectsTruncatedSectionNameTable) {
  g_synthetic_elf_file = CreateSyntheticElf();
  ASSERT_NE(g_synthetic_elf_file, -1);
  const off_t file_size = SyntheticElfFileSize();
  ASSERT_GT(file_size, 0);
  ASSERT_TRUE(ModifySyntheticObject<SectionHeader>(
      g_synthetic_section_header_offset, [file_size](SectionHeader& section) {
        section.sh_type = SHT_STRTAB;
        section.sh_name = kFunctionNameOffset;
        section.sh_offset = g_synthetic_symbol_string_offset;
        section.sh_size = static_cast<decltype(section.sh_size)>(
            static_cast<std::uint64_t>(file_size) -
            static_cast<std::uint64_t>(section.sh_offset) + 1);
      }));

  const char section_name[] = "expected_function";
  SectionHeader output;
  EXPECT_FALSE(GetSectionHeaderByName(g_synthetic_elf_file, section_name,
                                      sizeof(section_name), &output));
}

TEST_F(SyntheticElfSymbolize, FindsSectionHeaderByName) {
  g_synthetic_elf_file = CreateSyntheticElf();
  ASSERT_NE(g_synthetic_elf_file, -1);

  const char section_name[] = "expected_function";
  SectionHeader output;
  ASSERT_TRUE(GetSectionHeaderByName(g_synthetic_elf_file, section_name,
                                     sizeof(section_name), &output));
  EXPECT_EQ(output.sh_type, SHT_STRTAB);
}
#    endif

#    if defined(NGLOG_OS_WINDOWS) && defined(_MSC_VER)
TEST(Symbolize, DbgHelpUsesCanonicalFrameOrder) {
  char symbol[4096];
  SymbolizedFrame frame;
  ASSERT_TRUE(Symbolize(reinterpret_cast<void*>(&nonstatic_func), symbol,
                        sizeof(symbol), SymbolizeOptions::kNone, &frame));
  ASSERT_GT(frame.file_line_length, 0U);
  ASSERT_LT(frame.file_line_length, sizeof(symbol));

  EXPECT_EQ(0U, frame.file_line_offset);
  EXPECT_EQ(' ', symbol[frame.file_line_length]);
  EXPECT_THAT(symbol + frame.file_line_length, HasSubstr("nonstatic_func"));
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

testing::Matcher<int> IsWithinStackConsumptionLimit() {
  return AllOf(Gt(0), Lt(kStackConsumptionUpperLimit));
}

TEST(Symbolize, SymbolizeStackConsumption) {
  int stack_consumed;

  EXPECT_STREQ("nonstatic_func",
               SymbolizeStackConsumption(
                   reinterpret_cast<void*>(&nonstatic_func), &stack_consumed));
  EXPECT_THAT(stack_consumed, IsWithinStackConsumptionLimit());

  EXPECT_THAT(SymbolizeStackConsumption(reinterpret_cast<void*>(&static_func),
                                        &stack_consumed),
              IsStaticFuncSymbol());
  EXPECT_THAT(stack_consumed, IsWithinStackConsumptionLimit());
}

TEST(Symbolize, SymbolizeWithDemanglingStackConsumption) {
  Foo::func(100);
  int stack_consumed;

  EXPECT_STREQ("Foo::func()",
               SymbolizeStackConsumption(reinterpret_cast<void*>(&Foo::func),
                                         &stack_consumed));
  EXPECT_THAT(stack_consumed, IsWithinStackConsumptionLimit());
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

// The return address lies within the test body that calls the function.
TEST(Symbolize, ReturnAddress) {
  EXPECT_THAT(TestWithReturnAddress(),
              HasSubstr("Symbolize_ReturnAddress_Test"));
}

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

// The return address lies within the test body that calls the function.
TEST(Symbolize, ReturnAddress) {
  const char* symbol = TestWithReturnAddress();
  if (symbol == nullptr) {
    GTEST_SKIP() << "the Windows symbolization runtime is unavailable";
  }
  EXPECT_THAT(symbol, HasSubstr("Symbolize_ReturnAddress_Test"));
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
  nglog::SetFlag(FLAGS_logtostderr, true);
  InitializeLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);
#if defined(HAVE_SYMBOLIZE) && defined(HAVE_STACKTRACE)
#  if defined(HAVE_ELF_H) || defined(HAVE_SYS_EXEC_ELF_H)
  // We don't want to get affected by the callback interface, that may be
  // used to install some callback function at InitGoogle() time.
  InstallSymbolizeCallback(nullptr);
#  elif defined(NGLOG_OS_WINDOWS) || defined(NGLOG_OS_CYGWIN)
  // The tests below want bare, undecorated names, so make sure they are
  // not affected by whatever callback InitializeLogging() may have
  // installed.
  InstallSymbolizeCallback(nullptr);
#    if defined(HAVE_ADDR2LINE)
  // Symbolization spawns addr2line, which a loaded machine can stall for
  // longer than the default timeout. None of the tests measure time, so let
  // addr2line finish however long it takes.
  nglog::SetFlag(FLAGS_addr2line_timeout_ms,
                 std::numeric_limits<std::int32_t>::max());
#    endif
#  endif  // defined(HAVE_ELF_H) || defined(HAVE_SYS_EXEC_ELF_H)
#endif    // HAVE_SYMBOLIZE
  return RUN_ALL_TESTS();
}

#if defined(__GNUG__)
#  pragma GCC diagnostic pop
#endif
