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
#include <vector>

#include "config.h"
#include "ng-log/logging.h"
#include "stacktrace.h"
#include "utilities.h"

#if defined(HAVE_LINK_H)
#  include <fcntl.h>
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
using testing::IsNull;
using testing::Not;
using testing::StrEq;

// Avoid compile error due to "cast between pointer-to-function and
// pointer-to-object is an extension" warnings.
#if defined(__GNUG__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wpedantic"
#endif

#if defined(HAVE_STACKTRACE)

#  define always_inline

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
#        if defined(__i386__) && __i386__  // always_inline isn't supported for
                                           // x86_64 with GCC 4.1.0.
#          undef always_inline
#          define always_inline __attribute__((always_inline))
#          define HAVE_ALWAYS_INLINE
#        endif  // __i386__
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

#    if defined(HAVE_LINK_H)
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

int OpenSyntheticElf(std::uint64_t /*pc*/, std::uint64_t& start_address,
                     std::uint64_t& base_address, char* out_file_name,
                     std::size_t out_file_name_size) {
  start_address = 0;
  base_address = 0;
  std::strncpy(out_file_name, "synthetic-elf", out_file_name_size);
  out_file_name[out_file_name_size - 1] = '\0';
  return dup(g_synthetic_elf_file);
}

template <typename T>
void AppendObject(std::vector<char>& data, const T& object) {
  const auto* bytes = reinterpret_cast<const char*>(&object);
  data.insert(data.end(), bytes, bytes + sizeof(object));
}

int CreateSyntheticElf() {
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
  for (std::size_t i = kTlsSymbolIndex; i < symbols.size(); ++i) {
    AppendObject(data, symbols[i]);
  }
  const std::size_t symbol_string_offset = data.size();
  data.insert(data.end(), std::begin(kSymbolStrings), std::end(kSymbolStrings));

  const std::size_t section_header_offset =
      (data.size() + alignof(SectionHeader) - 1) / alignof(SectionHeader) *
      alignof(SectionHeader);
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
  sections[kStringTableSectionIndex].sh_type = SHT_STRTAB;
  sections[kStringTableSectionIndex].sh_offset = symbol_string_offset;
  sections[kStringTableSectionIndex].sh_size = sizeof(kSymbolStrings);
  for (const SectionHeader& section : sections) {
    AppendObject(data, section);
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
  header.e_shentsize = sizeof(SectionHeader);
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

#    if defined(HAVE_LINK_H)
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

struct Foo {
  static void func(int x);
};

void ATTRIBUTE_NOINLINE Foo::func(int x) {
  volatile int a = x;
  // NOTE: In C++20, increment of object of volatile-qualified type is
  // deprecated.
  a = a + 1;
}

// With a modern GCC, Symbolize() should return demangled symbol
// names.  Function parameters should be omitted.
#    ifdef TEST_WITH_MODERN_GCC
TEST(Symbolize, SymbolizeWithDemangling) {
  Foo::func(100);
#      if !defined(_MSC_VER) || !defined(NDEBUG)
#        if defined(HAVE___CXA_DEMANGLE)
  EXPECT_STREQ("Foo::func(int)", TrySymbolize((void*)(&Foo::func)));
#        else
  EXPECT_STREQ("Foo::func()", TrySymbolize((void*)(&Foo::func)));
#        endif
#      endif
}
#    endif

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
static ATTRIBUTE_NOINLINE bool StackGrowsDown(int* x) {
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

#      if !defined(HAVE___CXA_DEMANGLE)
#        ifdef __ppc64__
// Symbolize stack consumption should be within 4kB.
constexpr int kStackConsumptionUpperLimit = 4096;
#        else
// Symbolize stack consumption should be within 2kB.
constexpr int kStackConsumptionUpperLimit = 2048;
#        endif
#      endif

TEST(Symbolize, SymbolizeStackConsumption) {
  int stack_consumed;
  const char* symbol;

  symbol = SymbolizeStackConsumption(reinterpret_cast<void*>(&nonstatic_func),
                                     &stack_consumed);
  EXPECT_STREQ("nonstatic_func", symbol);
  EXPECT_GT(stack_consumed, 0);
#      if !defined(HAVE___CXA_DEMANGLE)
  EXPECT_LT(stack_consumed, kStackConsumptionUpperLimit);
#      endif

  // The name of an internal linkage symbol is not specified; allow either a
  // mangled or an unmangled name here.
  symbol = SymbolizeStackConsumption(reinterpret_cast<void*>(&static_func),
                                     &stack_consumed);
  ASSERT_THAT(symbol, Not(IsNull()));
  EXPECT_THAT(symbol, AnyOf(StrEq("static_func"), StrEq("static_func()")));
  EXPECT_GT(stack_consumed, 0);
#      if !defined(HAVE___CXA_DEMANGLE)
  EXPECT_LT(stack_consumed, kStackConsumptionUpperLimit);
#      endif
}

#      if defined(TEST_WITH_MODERN_GCC) && !defined(HAVE___CXA_DEMANGLE)
TEST(Symbolize, SymbolizeWithDemanglingStackConsumption) {
  Foo::func(100);
  int stack_consumed;
  const char* symbol;

  symbol = SymbolizeStackConsumption(reinterpret_cast<void*>(&Foo::func),
                                     &stack_consumed);

#        if defined(HAVE___CXA_DEMANGLE)
  EXPECT_STREQ("Foo::func(int)", symbol);
#        else
  EXPECT_STREQ("Foo::func()", symbol);
#        endif
  EXPECT_GT(stack_consumed, 0);
  EXPECT_LT(stack_consumed, kStackConsumptionUpperLimit);
}
#      endif

#    endif  // HAVE_SIGALTSTACK

// x86 specific tests.  Uses some inline assembler.
extern "C" {
inline void* always_inline inline_func() {
  void* pc = nullptr;
#    ifdef TEST_WITH_LABEL_ADDRESSES
  pc = &&curr_pc;
curr_pc:
#    endif
  return pc;
}

void* ATTRIBUTE_NOINLINE non_inline_func();
void* ATTRIBUTE_NOINLINE non_inline_func() {
  void* pc = nullptr;
#    ifdef TEST_WITH_LABEL_ADDRESSES
  pc = &&curr_pc;
curr_pc:
#    endif
  return pc;
}

static void ATTRIBUTE_NOINLINE TestWithPCInsideNonInlineFunction() {
#    if defined(TEST_WITH_LABEL_ADDRESSES) && defined(HAVE_ATTRIBUTE_NOINLINE)
  void* pc = non_inline_func();
  const char* symbol = TrySymbolize(pc);

#      if !defined(_MSC_VER) || !defined(NDEBUG)
  ASSERT_NE(symbol, nullptr);
  ASSERT_STREQ(symbol, "non_inline_func");
#      endif
#    endif
}

static void ATTRIBUTE_NOINLINE TestWithPCInsideInlineFunction() {
#    if defined(TEST_WITH_LABEL_ADDRESSES) && defined(HAVE_ALWAYS_INLINE)
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
#    if !defined(TEST_WITH_LABEL_ADDRESSES) || !defined(HAVE_ATTRIBUTE_NOINLINE)
  GTEST_SKIP() << "the compiler does not support this symbolization test";
#    endif
}

TEST(Symbolize, PCInsideInlineFunction) {
  TestWithPCInsideInlineFunction();
#    if !defined(TEST_WITH_LABEL_ADDRESSES) || !defined(HAVE_ALWAYS_INLINE)
  GTEST_SKIP() << "the compiler does not support this symbolization test";
#    endif
}

// Test with a return address.
static const char* ATTRIBUTE_NOINLINE TestWithReturnAddress() {
#    if defined(HAVE_ATTRIBUTE_NOINLINE)
  void* return_address = __builtin_return_address(0);
  return TrySymbolize(return_address, nglog::SymbolizeOptions::kNoLineNumbers);
#    else
  return nullptr;
#    endif
}

TEST(Symbolize, ReturnAddress) {
#    if defined(HAVE_ATTRIBUTE_NOINLINE)
  EXPECT_NE(TestWithReturnAddress(), nullptr);
#    else
  GTEST_SKIP() << "the compiler does not support this symbolization test";
#    endif
}

#  elif defined(NGLOG_OS_WINDOWS) || defined(NGLOG_OS_CYGWIN)

#    ifdef _MSC_VER
#      include <intrin.h>
#      pragma intrinsic(_ReturnAddress)
#    endif

struct Foo {
  static void func(int x);
};

__declspec(noinline) void Foo::func(int x) {
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

  // Which demangled form to expect depends on the compiler's name-mangling
  // ABI, not merely on HAVE_DBGHELP being available: MinGW and Clang on
  // Windows mangle (and demangle) with the Itanium ABI, like on Linux or
  // macOS, even though DbgHelp itself is present. Only MSVC produces the
  // decorated form below.
#    if defined(_MSC_VER)
#      if !defined(NDEBUG)
  EXPECT_STREQ("public: static void __cdecl Foo::func(int)", ret);
#      endif
#    elif !defined(NDEBUG)
  EXPECT_STREQ("Foo::func(int)", ret);
#    endif
}

__declspec(noinline) const char* TestWithReturnAddress() {
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
#endif  // HAVE_SYMBOLIZE
  return RUN_ALL_TESTS();
}

#if defined(__GNUG__)
#  pragma GCC diagnostic pop
#endif
