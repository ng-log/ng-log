// SPDX-FileCopyrightText: 2006-2024 Google Inc.
// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Satoru Takabayashi
//
// Unit tests for functions in demangle.c.

#include "demangle.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>  // IWYU pragma: keep

#include "config.h"
#include "ng-log/flags.h"
#include "ng-log/logging.h"
#include "ng-log/platform.h"

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

NGLOG_DEFINE(bool, demangle_filter, false,
             "Run demangle_unittest in filter mode");

using namespace nglog;
using testing::Contains;
using testing::Each;
using testing::HasSubstr;
using testing::Not;
using testing::StrEq;

// A wrapper function for Demangle() to make the unit test simple.
static const char* DemangleIt(const char* const mangled) {
  static char demangled[4096];
  if (Demangle(mangled, demangled, sizeof(demangled))) {
    return demangled;
  } else {
    return mangled;
  }
}

#if defined(NGLOG_OS_WINDOWS)

#  if defined(HAVE_DBGHELP) && defined(_MSC_VER) && !defined(NDEBUG)
static const char* DemangleWithSystemIt(const char* const mangled) {
  static char demangled[4096];
  if (DemangleWithSystem(mangled, demangled, sizeof(demangled))) {
    return demangled;
  }
  return mangled;
}

TEST(Demangle, Windows) {
  EXPECT_THAT(DemangleWithSystemIt("?func@Foo@@SAXH@Z"),
              StrEq("public: static void __cdecl Foo::func(int)"));
  EXPECT_THAT(DemangleWithSystemIt("@ILT+1105(?func@Foo@@SAXH@Z)"),
              StrEq("public: static void __cdecl Foo::func(int)"));
  EXPECT_THAT(DemangleWithSystemIt("?foobarArray@@YAHQAH@Z"),
              StrEq("int __cdecl foobarArray(int * const)"));
}
#  endif

#else

// Large enough for every successfully demangled name in these tests.
constexpr std::size_t kDemangleBufferSize = 256;

MATCHER_P(DemanglesTo, expected,
          std::string{negation ? "does not demangle to a name that "
                               : "demangles to a name that "} +
              testing::DescribeMatcher<std::string>(expected)) {
  const std::string mangled{arg};
  char demangled[kDemangleBufferSize];
  if (!Demangle(mangled.c_str(), demangled, sizeof(demangled))) {
    *result_listener << "cannot be demangled";
    return false;
  }
  *result_listener << "demangles to \"" << demangled << '"';
  return testing::ExplainMatchResult(
      testing::MatcherCast<std::string>(expected), std::string{demangled},
      result_listener);
}

testing::Matcher<std::string> FailsToDemangle() {
  return Not(DemanglesTo(testing::_));
}

// Test corner cases of boundary conditions.
TEST(Demangle, CornerCases) {
  const std::size_t size = 10;
  char tmp[size] = {0};
  const char* demangled = "foobar()";
  const char* mangled = "_Z6foobarv";
  EXPECT_TRUE(Demangle(mangled, tmp, sizeof(tmp)));
  // sizeof("foobar()") == size - 1
  EXPECT_THAT(tmp, StrEq(demangled));
  EXPECT_TRUE(Demangle(mangled, tmp, size - 1));
  EXPECT_THAT(tmp, StrEq(demangled));
  EXPECT_FALSE(Demangle(mangled, tmp, size - 2));  // Not enough.
  EXPECT_FALSE(Demangle(mangled, tmp, 1));
  EXPECT_FALSE(Demangle(mangled, tmp, 0));
  EXPECT_FALSE(Demangle(mangled, nullptr, 0));  // Should not cause SEGV.
}

TEST(Demangle, CxxSymbols) {
  const char* const mangled_symbols[] = {
      "_Z6foobarv",
      "_ZL3Foov",
      "_ZN3Foo3BarC1Ev",
      "_ZNSt6vectorIiSaIiEEC1Ev",
  };

  EXPECT_THAT(mangled_symbols, Each(DemanglesTo(Not(HasSubstr("_Z")))));
}

// Test handling of functions suffixed with .clone.N, which is used by GCC
// 4.5.x, and .constprop.N and .isra.N, which are used by GCC 4.6.x.  These
// suffixes are used to indicate functions which have been cloned during
// optimization.  We ignore these suffixes.
TEST(Demangle, Clones) {
  EXPECT_THAT("_ZL3Foov", DemanglesTo("Foo()"));
  EXPECT_THAT("_ZL3Foov.clone.3", DemanglesTo("Foo()"));
  EXPECT_THAT("_ZL3Foov.constprop.80", DemanglesTo("Foo()"));
  EXPECT_THAT("_ZL3Foov.isra.18", DemanglesTo("Foo()"));
  EXPECT_THAT("_ZL3Foov.isra.2.constprop.18", DemanglesTo("Foo()"));
  EXPECT_THAT("_ZL3Foov.vendor_suffix", DemanglesTo("Foo()"));
}

TEST(Demangle, UsesSignalSafeParserByDefault) {
  EXPECT_THAT("_Z1fi", DemanglesTo("f()"));
}

TEST(Demangle, UsesSystemDemanglerOnDemand) {
#  if defined(HAVE___CXA_DEMANGLE)
  char tmp[64];
  EXPECT_TRUE(DemangleWithSystem("_Z1fi", tmp, sizeof(tmp)));
  EXPECT_THAT(tmp, StrEq("f(int)"));
#  else
  GTEST_SKIP() << "no system demangler available";
#  endif
}

TEST(Demangle, RejectsInvalidBufferArguments) {
  char tmp[1];
  EXPECT_FALSE(Demangle("_ZN1N1fE", tmp, sizeof(tmp)));
  EXPECT_FALSE(Demangle("_ZN1N1fE", nullptr, 0));
  EXPECT_FALSE(Demangle(nullptr, tmp, sizeof(tmp)));
  EXPECT_FALSE(Demangle(nullptr, nullptr, 0));
  EXPECT_FALSE(DemangleWithSystem("_Z1fi", nullptr, sizeof(tmp)));
  EXPECT_FALSE(DemangleWithSystem(nullptr, tmp, sizeof(tmp)));
}

TEST(Demangle, RejectsExcessivelyNestedTypes) {
  constexpr std::size_t kMaximumNestedTypes = 64;
  std::string mangled = "_Z1f";
  mangled.append(kMaximumNestedTypes, 'P');
  mangled += "v";

  EXPECT_THAT(mangled, FailsToDemangle());
}

std::string NestedLocalName(std::size_t levels) {
  std::string mangled = "_Z";
  for (std::size_t index = 0; index < levels; ++index) {
    mangled += "Z1fvE";
  }
  return mangled + "1x";
}

// The output buffer holds the demangled name of any nesting depth used
// below, so that only the nesting limits can reject a name.
constexpr std::size_t kNestedNameBufferSize = 4096;

TEST(Demangle, AcceptsModeratelyNestedLocalEncodings) {
  constexpr std::size_t kModerateNesting = 8;
  char tmp[kNestedNameBufferSize];
  EXPECT_TRUE(
      Demangle(NestedLocalName(kModerateNesting).c_str(), tmp, sizeof(tmp)));
}

TEST(Demangle, RejectsExcessivelyNestedLocalEncodings) {
  // Exceeds every nesting limit of the parser.
  constexpr std::size_t kExcessiveNesting = 65;
  char tmp[kNestedNameBufferSize];
  EXPECT_FALSE(
      Demangle(NestedLocalName(kExcessiveNesting).c_str(), tmp, sizeof(tmp)));
}

struct DemangleCase {
  std::string mangled;
  std::string demangled;
};

std::string MangledTestName(const std::string& mangled) {
  std::string name = "Mangled";
  for (const char character : mangled) {
    if ((character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') || character == '_') {
      name.push_back(character);
    } else {
      name.push_back('_');
    }
  }
  return name;
}

std::string DemangleCaseName(const testing::TestParamInfo<DemangleCase>& info) {
  return MangledTestName(info.param.mangled);
}

class DemangleTest : public testing::TestWithParam<DemangleCase> {};

TEST_P(DemangleTest, ParsesItaniumName) {
  EXPECT_THAT(GetParam().mangled, DemanglesTo(GetParam().demangled));
}

INSTANTIATE_TEST_SUITE_P(
    ItaniumAbi, DemangleTest,
    testing::Values(DemangleCase{"_ZNR1A1fEv", "A::f() &"},
                    DemangleCase{"_ZNO1A1gEv", "A::g() &&"},
                    DemangleCase{"_ZN1AmiB3tagEv", "A::operator-()"},
                    DemangleCase{"_Z1fSsB1XS_", "f()"},
                    DemangleCase{"_Z1fIiEDtsrNT_1XE1yET_", "f<>()"},
                    DemangleCase{"_Z1fIiEDtptfp_gssr1A1BE1xET_", "f<>()"},
                    DemangleCase{"_Z1fIiEDTadsrT_onmiET_", "f<>()"},
                    DemangleCase{"_Z1fIXspLi1EEEvv", "f<>()"},
                    DemangleCase{"_Z1fIiEDtgs1xEv", "f<>()"},
                    DemangleCase{"_Z1fIiEDtsrT_IiE1xET_", "f<>()"},
                    DemangleCase{"_ZN1SIiE1xMUlvE_E", "S<>::x"},
                    DemangleCase{"_ZN1AIUlvE_E1BEv", "A<>::B()"},
                    DemangleCase{"_ZNSt17_Function_handlerIFvvEZ4mainEUlvE_E9_"
                                 "M_invokeERKSt9_Any_data",
                                 "std::_Function_handler<>::_M_invoke()"},
                    DemangleCase{"_ZZN1S1fEiiEd0_UlvE_", "S::f()::"},
                    DemangleCase{"_Z3foov.vendor_suffix", "foo()"},
                    DemangleCase{"_Z3fooA999999999999999999999_i", "foo()"},
                    DemangleCase{"_ZZ1fvE1x__2147483648_", "f()::x"},
                    DemangleCase{"_ZZ1fvE1x__9223372036854775807_", "f()::x"},
                    DemangleCase{"_Z1fI1AI1AI1AI1AI1AI1AI1AIiEEEEEEEEvv",
                                 "f<>()"}),
    DemangleCaseName);

INSTANTIATE_TEST_SUITE_P(
    ModernItaniumNames, DemangleTest,
    testing::Values(
        DemangleCase{"_Z3fooDu", "foo()"}, DemangleCase{"_Z3fooDi", "foo()"},
        DemangleCase{"_Z3fooDs", "foo()"}, DemangleCase{"_Z3fooDh", "foo()"},
        DemangleCase{"_Z3fooDc", "foo()"}, DemangleCase{"_Z3fooDa", "foo()"},
        DemangleCase{"_Z3fooDf", "foo()"}, DemangleCase{"_Z3fooDF32_", "foo()"},
        DemangleCase{"_Z3fooDF32x", "foo()"},
        DemangleCase{"_Z3fooDF16b", "foo()"},
        DemangleCase{"_Z3fooDB8_", "foo()"},
        DemangleCase{"_Z3fooDU8_", "foo()"},
        DemangleCase{"_Z3fooDAiv", "foo()"},
        DemangleCase{"_Z3fooDSDAiv", "foo()"},
        DemangleCase{"_Z3fooDRjv", "foo()"},
        DemangleCase{"_Z3foou3barIiiEv", "foo()"},
        DemangleCase{"_Z1fIiEDTLDnEEv", "f<>()"},
        DemangleCase{"_Z6prefixIiEDTpp_fp_ET_", "prefix<>()"},
        DemangleCase{"_Z10align_typeIiEDTatT_ES0_", "align_type<>()"},
        DemangleCase{"_Z4noexIiEDTnxfp_ET_", "noex<>()"},
        DemangleCase{"_Z8new_exprIiEDTnw_T_pifp_EES0_", "new_expr<>()"},
        DemangleCase{"_Z1fILA4_KwEEvv", "f<>()"},
        DemangleCase{"_Z4foldIJiiEEDTflplfp_EDpT_", "fold<>()"},
        DemangleCase{"_Z9qualifiedI1AEDtsrT_5valueEv", "qualified<>()"},
        DemangleCase{"_Z6nestedI1AEDtsrNT_5innerE5valueEv", "nested<>()"}),
    DemangleCaseName);

class InvalidDemangleTest : public testing::TestWithParam<std::string> {};

TEST_P(InvalidDemangleTest, RejectsName) {
  EXPECT_THAT(GetParam(), FailsToDemangle());
}

INSTANTIATE_TEST_SUITE_P(ItaniumAbi, InvalidDemangleTest,
                         testing::Values("_ZC1v", "_ZD1v", "_ZCI11Av", "_Z01fv",
                                         "_Z02foov", "_Z1fA01_i", "_Z1fAn1_i",
                                         "_ZZ1fvE1x_10", "_ZZ1fvE1x__1_",
                                         "_ZZ1fvE1x__9223372036854775808_",
                                         "_ZZ1fvE1x__n9223372036854775808_",
                                         "_Z3foov.", "_Z", "_Z1fIXplLi1EEEvv",
                                         "_Z1fILiEEvv", "_Z1fFvvROE"),
                         [](const testing::TestParamInfo<std::string>& info) {
                           return MangledTestName(info.param);
                         });

// The corpus falls back to the mangled name whenever demangling fails, which
// keeps names that are not mangled, such as C linkage symbols, untouched.
class CorpusDemangleTest : public testing::TestWithParam<DemangleCase> {};

TEST_P(CorpusDemangleTest, DemanglesExpectedName) {
  EXPECT_THAT(DemangleIt(GetParam().mangled.c_str()),
              StrEq(GetParam().demangled));
}

// Test cases for demangle_unittest. Each entry consists of a mangled and an
// expected demangled symbol name.

INSTANTIATE_TEST_SUITE_P(ConstructorsAndDestructors, CorpusDemangleTest,
                         testing::Values(
                             // Constructors and destructors.
                             DemangleCase{"_ZN3FooC1Ev", "Foo::Foo()"},
                             DemangleCase{"_ZN3FooD1Ev", "Foo::~Foo()"},
                             DemangleCase{"_ZNSoD0Ev",
                                          "std::ostream::~ostream()"}),
                         DemangleCaseName);

// G++ extensions.
INSTANTIATE_TEST_SUITE_P(
    GxxExtensions, CorpusDemangleTest,
    testing::Values(DemangleCase{"_ZTCN10LogMessage9LogStreamE0_So",
                                 "LogMessage::LogStream"},
                    DemangleCase{"_ZTv0_n12_N10LogMessage9LogStreamD0Ev",
                                 "LogMessage::LogStream::~LogStream()"},
                    DemangleCase{"_ZThn4_N7icu_3_410UnicodeSetD0Ev",
                                 "icu_3_4::UnicodeSet::~UnicodeSet()"}),
    DemangleCaseName);

INSTANTIATE_TEST_SUITE_P(AdditionalAbiProductions, CorpusDemangleTest,
                         testing::Values(DemangleCase{"_Z1fTs1Av", "f()"},
                                         DemangleCase{"_Z1fTu1Av", "f()"},
                                         DemangleCase{"_Z1fTe1Av", "f()"},
                                         DemangleCase{"_ZTch0_h0_1fv", "f()"},
                                         DemangleCase{"_ZTh0_1fv", "f()"},
                                         DemangleCase{"_ZTv0_n0_1fv", "f()"},
                                         DemangleCase{"_ZGA1fv", "f()"},
                                         DemangleCase{"_ZTI1A", "A"},
                                         DemangleCase{"_ZTV1A", "A"}),
                         DemangleCaseName);

// A bug in g++'s C++ ABI version 2 (-fabi-version=2).
INSTANTIATE_TEST_SUITE_P(
    GccAbiVersionTwo, CorpusDemangleTest,
    testing::Values(DemangleCase{
        "_ZN7NSSInfoI5groupjjXadL_Z10getgrgid_rEELZ19nss_getgrgid_r_nameEEC1Ei",
        "NSSInfo<>::NSSInfo()"}),
    DemangleCaseName);

// C linkage symbol names. Should keep them untouched.
INSTANTIATE_TEST_SUITE_P(CLinkageNames, CorpusDemangleTest,
                         testing::Values(DemangleCase{"main", "main"},
                                         DemangleCase{"Demangle", "Demangle"},
                                         DemangleCase{"_ZERO", "_ZERO"}),
                         DemangleCaseName);

// Cast operator.
INSTANTIATE_TEST_SUITE_P(
    CastOperators, CorpusDemangleTest,
    testing::Values(DemangleCase{"_Zcviv", "operator int()"},
                    DemangleCase{"_ZN3foocviEv", "foo::operator int()"}),
    DemangleCaseName);

// Versioned symbols.
INSTANTIATE_TEST_SUITE_P(
    VersionedSymbols, CorpusDemangleTest,
    testing::Values(DemangleCase{"_Z3Foo@GLIBCXX_3.4", "Foo@GLIBCXX_3.4"},
                    DemangleCase{"_Z3Foo@@GLIBCXX_3.4", "Foo@@GLIBCXX_3.4"}),
    DemangleCaseName);

// Abbreviations.
INSTANTIATE_TEST_SUITE_P(
    Abbreviations, CorpusDemangleTest,
    testing::Values(DemangleCase{"_ZNSaE", "std::allocator"},
                    DemangleCase{"_ZNSbE", "std::basic_string"},
                    DemangleCase{"_ZNSdE", "std::iostream"},
                    DemangleCase{"_ZNSiE", "std::istream"},
                    DemangleCase{"_ZNSoE", "std::ostream"},
                    DemangleCase{"_ZNSsE", "std::string"}),
    DemangleCaseName);

// Substitutions. We just replace them with ?.
INSTANTIATE_TEST_SUITE_P(
    Substitutions, CorpusDemangleTest,
    testing::Values(DemangleCase{"_ZN3fooS_E", "foo::?"},
                    DemangleCase{"_ZN3foo3barS0_E", "foo::bar::?"},
                    DemangleCase{"_ZNcvT_IiEEv", "operator ?<>()"}),
    DemangleCaseName);

// "<< <" case.
INSTANTIATE_TEST_SUITE_P(LessThanLessThan, CorpusDemangleTest,
                         testing::Values(DemangleCase{"_ZlsI3fooE",
                                                      "operator<< <>"}),
                         DemangleCaseName);

// ABI tags.
INSTANTIATE_TEST_SUITE_P(AbiTags, CorpusDemangleTest,
                         testing::Values(DemangleCase{"_Z1AB3barB3foo", "A"},
                                         DemangleCase{"_ZN3fooL3barB5cxx11E",
                                                      "foo::bar"}),
                         DemangleCaseName);

// Random things we found interesting.
INSTANTIATE_TEST_SUITE_P(
    InterestingNames, CorpusDemangleTest,
    testing::Values(
        DemangleCase{"_ZN3FooISt6vectorISsSaISsEEEclEv", "Foo<>::operator()()"},
        DemangleCase{"_ZTI9Callback1IiE", "Callback1<>"},
        DemangleCase{"_ZN7icu_3_47UMemorynwEj",
                     "icu_3_4::UMemory::operator new()"},
        DemangleCase{"_ZNSt6vectorIbE9push_backE", "std::vector<>::push_back"},
        DemangleCase{"_ZNSt6vectorIbSaIbEE9push_backEb",
                     "std::vector<>::push_back()"},
        DemangleCase{"_ZlsRSoRK15PRIVATE_Counter", "operator<<()"},
        DemangleCase{"_ZSt6fill_nIPPN9__gnu_cxx15_Hashtable_"
                     "nodeISt4pairIKPKcjEEEjS8_ET_SA_T0_RKT1_",
                     "std::fill_n<>()"},
        DemangleCase{"_ZZ3FoovE3Bar", "Foo()::Bar"},
        DemangleCase{"_ZGVZ7UpTimervE8up_timer", "UpTimer()::up_timer"}),
    DemangleCaseName);

INSTANTIATE_TEST_SUITE_P(
    GccDemangleCases, CorpusDemangleTest,
    testing::Values(
        // Test cases from gcc-4.1.0/libstdc++-v3/testsuite/demangle.
        // Collected by:
        // % grep verify_demangle **/*.cc | perl -nle 'print $1 if /"(_Z.*?)"/'
        // |
        //   sort | uniq
        //
        // Note that the following symbols are invalid.
        // That's why they are not demangled.
        // - _ZNZN1N1fEiE1X1gE
        // - _ZNZN1N1fEiE1X1gEv
        // - _Z1xINiEE
        DemangleCase{"_Z1fA37_iPS_", "f()"},
        DemangleCase{"_Z1fAszL_ZZNK1N1A1fEvE3foo_0E_i", "f()"},
        DemangleCase{"_Z1fI1APS0_PKS0_EvT_T0_T1_PA4_S3_M1CS8_", "f<>()"},
        DemangleCase{"_Z1fI1XENT_1tES2_", "f<>()"},
        DemangleCase{"_Z1fI1XEvPVN1AIT_E1TE", "f<>()"},
        DemangleCase{"_Z1fILi1ELc120EEv1AIXplT_cviLd4028ae147ae147aeEEE",
                     "f<>()"},
        DemangleCase{"_Z1fILi1ELc120EEv1AIXplT_cviLf3f800000EEE", "f<>()"},
        DemangleCase{"_Z1fILi5E1AEvN1CIXqugtT_Li0ELi1ELi2EEE1qE", "f<>()"},
        DemangleCase{"_Z1fILi5E1AEvN1CIXstN1T1tEEXszsrS2_1tEE1qE", "f<>()"},
        DemangleCase{"_Z1fILi5EEvN1AIXcvimlT_Li22EEE1qE", "f<>()"},
        DemangleCase{"_Z1fIiEvi", "f<>()"}, DemangleCase{"_Z1fKPFiiE", "f()"},
        DemangleCase{"_Z1fM1AFivEPS0_", "f()"},
        DemangleCase{"_Z1fM1AKFivE", "f()"},
        DemangleCase{"_Z1fM1AKFvvE", "f()"},
        DemangleCase{"_Z1fPFPA1_ivE", "f()"},
        DemangleCase{"_Z1fPFYPFiiEiE", "f()"},
        DemangleCase{"_Z1fPFvvEM1SFvvE", "f()"},
        DemangleCase{"_Z1fPKM1AFivE", "f()"}, DemangleCase{"_Z1fi", "f()"},
        DemangleCase{"_Z1fv", "f()"}, DemangleCase{"_Z1jM1AFivEPS1_", "j()"},
        DemangleCase{"_Z1rM1GFivEMS_KFivES_M1HFivES1_4whatIKS_E5what2IS8_ES3_",
                     "r()"},
        DemangleCase{"_Z1sPA37_iPS0_", "s()"},
        DemangleCase{"_Z1xINiEE", "_Z1xINiEE"},
        DemangleCase{"_Z3absILi11EEvv", "abs<>()"},
        DemangleCase{"_Z3foo3bar", "foo()"},
        DemangleCase{"_Z3foo5Hello5WorldS0_S_", "foo()"},
        DemangleCase{"_Z3fooA30_A_i", "foo()"},
        DemangleCase{"_Z3fooIA6_KiEvA9_KT_rVPrS4_", "foo<>()"},
        DemangleCase{"_Z3fooILi2EEvRAplT_Li1E_i", "foo<>()"},
        DemangleCase{"_Z3fooIiFvdEiEvv", "foo<>()"},
        DemangleCase{"_Z3fooPM2ABi", "foo()"}, DemangleCase{"_Z3fooc", "foo()"},
        DemangleCase{"_Z3kooPA28_A30_i", "koo()"},
        DemangleCase{
            "_Z3fooiPiPS_PS0_PS1_PS2_PS3_PS4_PS5_PS6_PS7_PS8_PS9_PSA_PSB_PSC_",
            "foo()"},
        DemangleCase{"_Z4makeI7FactoryiET_IT0_Ev", "make<>()"},
        DemangleCase{"_Z5firstI3DuoEvS0_", "first<>()"},
        DemangleCase{"_Z5firstI3DuoEvT_", "first<>()"},
        DemangleCase{"_Z9hairyfuncM1YKFPVPFrPA2_PM1XKFKPA3_ilEPcEiE",
                     "hairyfunc()"},
        DemangleCase{
            "_ZGVN5libcw24_GLOBAL__N_cbll.cc0ZhUKa23compiler_bug_"
            "workaroundISt6vectorINS_13omanip_id_tctINS_5debug32memblk_types_"
            "manipulator_data_ctEEESaIS6_EEE3idsE",
            "libcw::(anonymous namespace)::compiler_bug_workaround<>::ids"},
        DemangleCase{"_ZN12libcw_app_ct10add_optionIS_EEvMT_FvPKcES3_cS3_S3_",
                     "libcw_app_ct::add_option<>()"},
        DemangleCase{"_ZN1AIfEcvT_IiEEv", "A<>::operator ?<>()"},
        DemangleCase{"_ZN1N1TIiiE2mfES0_IddE", "N::T<>::mf()"},
        DemangleCase{"_ZN1N1fE", "N::f"}, DemangleCase{"_ZN1f1fE", "f::f"},
        DemangleCase{"_ZN3FooIA4_iE3barE", "Foo<>::bar"},
        DemangleCase{"_ZN5Arena5levelE", "Arena::level"},
        DemangleCase{"_ZN5StackIiiE5levelE", "Stack<>::level"},
        DemangleCase{
            "_ZN5libcw5debug13cwprint_usingINS_9_private_12GlobalObjectEEENS0_"
            "17cwprint_using_tctIT_EERKS5_MS5_KFvRSt7ostreamE",
            "libcw::debug::cwprint_using<>()"},
        DemangleCase{"_ZN6System5Sound4beepEv", "System::Sound::beep()"},
        DemangleCase{"_ZNKSt14priority_queueIP27timer_event_request_base_"
                     "ctSt5dequeIS1_SaIS1_EE13timer_greaterE3topEv",
                     "std::priority_queue<>::top()"},
        DemangleCase{
            "_ZNKSt15_Deque_iteratorIP15memory_block_stRKS1_PS2_EeqERKS5_",
            "std::_Deque_iterator<>::operator==()"},
        DemangleCase{
            "_ZNKSt17__normal_iteratorIPK6optionSt6vectorIS0_SaIS0_EEEmiERKS6_",
            "std::__normal_iterator<>::operator-()"},
        DemangleCase{"_ZNSbIcSt11char_traitsIcEN5libcw5debug27no_alloc_"
                     "checking_allocatorEE12_S_constructIPcEES6_T_S7_RKS3_",
                     "std::basic_string<>::_S_construct<>()"},
        DemangleCase{
            "_ZNSt13_Alloc_traitsISbIcSt18string_char_traitsIcEN5libcw5debug9_"
            "private_17allocator_adaptorIcSt24__default_alloc_"
            "templateILb0ELi327664E"
            "ELb1EEEENS5_IS9_S7_Lb1EEEE15_S_instancelessE",
            "std::_Alloc_traits<>::_S_instanceless"},
        DemangleCase{"_ZNSt3_In4wardE", "std::_In::ward"},
        DemangleCase{"_ZNZN1N1fEiE1X1gE", "_ZNZN1N1fEiE1X1gE"},
        DemangleCase{"_ZNZN1N1fEiE1X1gEv", "_ZNZN1N1fEiE1X1gEv"},
        DemangleCase{
            "_ZSt1BISt1DIP1ARKS2_PS3_ES0_IS2_RS2_PS2_ES2_ET0_T_SB_SA_PT1_",
            "std::B<>()"},
        DemangleCase{"_ZSt5state", "std::state"},
        DemangleCase{"_ZTI7a_class", "a_class"},
        DemangleCase{"_ZZN1N1fEiE1p", "N::f()::p"},
        DemangleCase{"_ZZN1N1fEiEs", "N::f()"},
        DemangleCase{"_ZlsRK1XS1_", "operator<<()"},
        DemangleCase{"_ZlsRKU3fooU4bart1XS0_", "operator<<()"},
        DemangleCase{"_ZlsRKU3fooU4bart1XS2_", "operator<<()"},
        DemangleCase{"_ZlsRSoRKSs", "operator<<()"},
        DemangleCase{"_ZngILi42EEvN1AIXplT_Li2EEE1TE", "operator-<>()"},
        DemangleCase{"_ZplR1XS0_", "operator+()"},
        DemangleCase{"_Zrm1XS_", "operator%()"}),
    DemangleCaseName);

INSTANTIATE_TEST_SUITE_P(TemplateArgumentPacks, CorpusDemangleTest,
                         testing::Values(
                             // Template argument packs can start with I or J.
                             DemangleCase{"_Z3addIIiEEvDpT_", "add<>()"},
                             DemangleCase{"_Z3addIJiEEvDpT_", "add<>()"}),
                         DemangleCaseName);

INSTANTIATE_TEST_SUITE_P(
    NestedTemplatePackExpansions, CorpusDemangleTest,
    testing::Values(
        // Nested templates with pack expansion.
        DemangleCase{
            "_ZSt13__invoke_implIvPFvPiEJDnEET_St14__invoke_otherOT0_DpOT1_",
            "std::__invoke_impl<>()"},
        DemangleCase{"_ZSt8__invokeIPFvPiEJDnEENSt15__invoke_resultIT_JDpT0_"
                     "EE4typeEOS4_DpOS5_",
                     "std::__invoke<>()"},
        DemangleCase{"_ZNSt6thread8_InvokerISt5tupleIJPFvPiEDnEEE9_M_"
                     "invokeIJLm0ELm1EEEEvSt12_Index_tupleIJXspT_EEE",
                     "std::thread::_Invoker<>::_M_invoke<>()"},
        DemangleCase{"_ZNSt6thread8_InvokerISt5tupleIJPFvPiEDnEEEclEv",
                     "std::thread::_Invoker<>::operator()()"},
        DemangleCase{"_ZNSt6thread11_State_implINS_8_"
                     "InvokerISt5tupleIJPFvPiEDnEEEEE6_M_runEv",
                     "std::thread::_State_impl<>::_M_run()"}),
    DemangleCaseName);

TEST(Demangle, VendorQualifiedTypes) {
  EXPECT_THAT("_Z3fooU3barIiiEi", DemanglesTo("foo()"));
}

TEST(Demangle, ModernOperatorNames) {
  EXPECT_THAT("_ZNK1SssERKS_", DemanglesTo("S::operator<=>()"));
}

TEST(Demangle, LiteralOperatorNames) {
  EXPECT_THAT("_Zli2_xe", DemanglesTo("operator\"\" _x()"));
}

TEST(Demangle, ComplexFloatingPointLiterals) {
  EXPECT_THAT("_Z1fILf3f800000_40000000EEvv", DemanglesTo("f<>()"));
}

TEST(Demangle, RequiresClauses) {
  EXPECT_THAT("_Z3fooIiQgtstT_Li0EEiS0_", DemanglesTo("foo<>()"));
}

TEST(Demangle, ThreadLocalSpecialNames) {
  EXPECT_THAT("_ZTWN2ns3varE", DemanglesTo("TLS wrapper function for ns::var"));
  EXPECT_THAT("_ZTHN2ns3varE", DemanglesTo("TLS init function for ns::var"));
}

#  if defined(HAVE_UNISTD_H) && defined(GTEST_HAS_DEATH_TEST)
namespace {

// Builds a template argument that nests |depth| conversions of decltype
// expressions, each of which the parser tries to read in two ways.
std::string NestedConversions(int depth) {
  std::string expression = "T_";
  for (int i = 0; i < depth; ++i) {
    expression = "cvDt" + expression + "E_E";
  }
  return "_Z1fIX" + expression + "EEv";
}

}  // namespace

// The backtracking parser retries alternatives of nested constructs, which
// takes exponential time for short inputs. Since the demangler runs in crash
// handlers, it must give up instead.
TEST(Demangle, BoundsTheWorkForNestedInputs) {
  // Reports a hang before the CTest timeout, with ample room for a loaded
  // machine.
  using namespace std::chrono_literals;
  constexpr std::chrono::seconds kTimeout = 20s;
  constexpr int kLocalNameDepth = 16;
  constexpr int kConversionDepth = 30;
  const std::string local_names =
      "_Z1f" + std::string(kLocalNameDepth, 'Z') + "v";
  const std::string conversions = NestedConversions(kConversionDepth);
  EXPECT_EXIT(
      {
        alarm(static_cast<unsigned>(kTimeout.count()));
        char out[kDemangleBufferSize];
        Demangle(local_names.c_str(), out, sizeof(out));
        Demangle(conversions.c_str(), out, sizeof(out));
        std::exit(EXIT_SUCCESS);
      },
      testing::ExitedWithCode(EXIT_SUCCESS), "");
}
#  endif

TEST(Demangle, FailedParseClearsOutput) {
  char tmp[8] = {'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x'};
  EXPECT_FALSE(Demangle("_Z", tmp, sizeof(tmp)));
  EXPECT_EQ(tmp[0], '\0');
}

// Output that overflowed the buffer before the parse failed must not remain,
// since it lacks its terminating null character.
TEST(Demangle, FailedParseAfterOverflowClearsOutput) {
  constexpr std::size_t kTlsInitOutputSize = 19;
  char tls_init[kTlsInitOutputSize];
  EXPECT_FALSE(Demangle("_ZTHE", tls_init, sizeof(tls_init)));
  EXPECT_EQ(tls_init[0], '\0');

  constexpr std::size_t kShortOutputSize = 4;
  char function[kShortOutputSize];
  EXPECT_FALSE(Demangle("_Z2fooDF32x", function, sizeof(function)));
  EXPECT_EQ(function[0], '\0');
}

TEST(Demangle, UnnamedTypes) { EXPECT_THAT("_Z1fN1SUt_E", DemanglesTo("f()")); }

TEST(Demangle, LambdaAndStructuredBindingNames) {
  EXPECT_THAT("_ZZ1giENKUlvE_clEv", DemanglesTo("g()::operator()()"));
  EXPECT_THAT("_ZDC1a1bE", DemanglesTo("[a, b]"));
}

TEST(Demangle, SourceNamesHavePositiveLength) {
  EXPECT_THAT("_Z0v", FailsToDemangle());
  EXPECT_THAT("_Z3foov!", FailsToDemangle());
}

TEST(Demangle, InheritingConstructors) {
  EXPECT_THAT("_ZN1DCI11AEv", DemanglesTo("D::A()"));
  EXPECT_THAT("_ZN1DCI21AEv", DemanglesTo("D::A()"));
}

TEST(Demangle, StructorVariants) {
  // GCC emits C4/C5 and D4/D5 for the unified and COMDAT-folded structors.
  EXPECT_THAT("_ZN1AC4Ev", DemanglesTo("A::A()"));
  EXPECT_THAT("_ZN1AC5Ev", DemanglesTo("A::A()"));
  EXPECT_THAT("_ZN1AD4Ev", DemanglesTo("A::~A()"));
  EXPECT_THAT("_ZN1AD5Ev", DemanglesTo("A::~A()"));
  // The ABI reserves D3 but no compiler emits it, and there is no C0.
  EXPECT_THAT("_ZN1AD3Ev", FailsToDemangle());
  EXPECT_THAT("_ZN1AC0Ev", FailsToDemangle());
}

TEST(Demangle, DefaultArgumentScopes) {
  // The parameter number is optional; its absence designates the last
  // parameter.
  EXPECT_THAT("_ZZ1fiEd_1x", DemanglesTo("f()::x"));
  EXPECT_THAT("_ZZ1fiEd0_1x", DemanglesTo("f()::x"));
  EXPECT_THAT("_ZZ1fiEd_NKUlvE_clEv", DemanglesTo("f()::operator()()"));
}

TEST(Demangle, StdQualifiedUnresolvedNames) {
  EXPECT_THAT("_Z1fIiEvNSt9enable_ifIXsrSt1A5valueEvE4typeE",
              DemanglesTo("f<>()"));
  EXPECT_THAT("_Z1fIiEvNSt9enable_ifIXsrSt1AI1BE5valueEvE4typeE",
              DemanglesTo("f<>()"));
  // Only a single qualifier level may follow the std abbreviation.
  EXPECT_THAT("_Z1fIiEvNSt9enable_ifIXsrSt1A1B5valueEvE4typeE",
              FailsToDemangle());
}

TEST(Demangle, TransactionSafeEntryPoints) {
  EXPECT_THAT("_ZGTt3foov", DemanglesTo("transaction clone for foo()"));
}

TEST(Demangle, DependentFunctionParameterReferences) {
  EXPECT_THAT("_Z1fIiEDtfp_ET_", DemanglesTo("f<>()"));
}

TEST(Demangle, DependentMemberExpressions) {
  EXPECT_THAT("_Z1fI1SEDtdtfp_1xET_", DemanglesTo("f<>()"));
}

TEST(Demangle, BracedExpressions) {
  EXPECT_THAT("_Z1fI1SEDTtlT_fp_EEi", DemanglesTo("f<>()"));
}

TEST(Demangle, RejectsDeeplyNestedBracedExpressions) {
  constexpr std::size_t kNestedBracedExpressions = 64;
  std::string mangled = "_Z1fI1SEDTtlT_";
  for (std::size_t index = 0; index < kNestedBracedExpressions; ++index) {
    mangled += "di1x";
  }
  mangled += "fp_EEi";

  EXPECT_THAT(mangled, FailsToDemangle());
}

TEST(Demangle, RequiresExpressions) {
  EXPECT_THAT("_Z1fI1SEDTrqXdtfp_1xEET_", DemanglesTo("f<>()"));
}

TEST(Demangle, FunctionTypeExceptionSpecifications) {
  EXPECT_THAT("_Z5takesPDoFvvE", DemanglesTo("takes()"));
}

TEST(Demangle, GuardAndTemporarySpecialNames) {
  EXPECT_THAT("_ZGV1x", DemanglesTo("x"));
  EXPECT_THAT("_ZGR1x_", DemanglesTo("x"));
}

TEST(Demangle, ExtendedLocalDiscriminators) {
  EXPECT_THAT("_ZZ1fvE1x__10_", DemanglesTo("f()::x"));
}

// Reconstructing a truncated constructor or destructor name must not read
// back output buffer bytes that were never written, since callers are not
// required to zero-initialize the output buffer before calling Demangle().
TEST(Demangle, CtorDtorTruncation) {
  // Deliberately left uninitialized so that tools such as Valgrind or
  // MemorySanitizer can catch reads of never-written bytes.
  char tmp[5];
  EXPECT_FALSE(Demangle("_ZN3Foo3BarC1Ev", tmp, sizeof(tmp)));
  EXPECT_FALSE(Demangle("_ZN3Foo3BarD1Ev", tmp, sizeof(tmp)));

  // Fuzzer-discovered input exercising the same truncation path through a
  // deeper nesting of prefixes.
  char tmp2[16];
  const char* const fuzzed =
      "_ZN7ppppppppppsppppppppppppppppppppppppppppppppppppppI00000E0SiD22L_"
      "ZTVZleA2_ZZ\x7f";
  EXPECT_FALSE(Demangle(fuzzed, tmp2, sizeof(tmp2)));
}

// A name whose demangled form does not fit the output buffer must be
// reported as a failure, not as a success with the output silently cut
// off, and in particular must never leave the output unterminated: the
// crash handler demangles the frames of a dumped stack trace into a
// fixed-size buffer, and an unterminated "success" previously made it
// abort (and then hang) in the middle of dumping. The symbol below is
// std::call_once()'s execution machinery, which is part of every such
// stack trace, and demangles to well over 256 characters.
TEST(Demangle, LongNameTruncation) {
  const char* const mangled =
      "_ZZNSt9once_flag18_Prepare_executionC1IZSt9call_onceIPFviP9siginfo_t"
      "PvEJRiRS4_RS5_EEvRS_OT_DpOT0_EUlvE_EERSC_ENKUlvE_clEv";

  // Large enough for any implementation to succeed, in which case the
  // result must be '\0'-terminated.
  char big[4096];
  if (Demangle(mangled, big, sizeof(big))) {
    EXPECT_THAT(big, Contains('\0'));
  }

  char small_buf[256];
  if (Demangle(mangled, small_buf, sizeof(small_buf))) {
    EXPECT_THAT(small_buf, Contains('\0'));
  }
}

#endif

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  nglog::ParseCommandLineFlags(&argc, &argv,
                               nglog::FlagParsingMode::kRemoveFlags);

  nglog::SetFlag(FLAGS_logtostderr, true);
  InitializeLogging(argv[0]);
  if (nglog::GetFlag(FLAGS_demangle_filter)) {
    // Read from cin and write to cout.
    std::string line;
    while (std::getline(std::cin, line, '\n')) {
      std::cout << DemangleIt(line.c_str()) << std::endl;
    }
    return 0;
  } else if (argc > 1) {
    std::cout << DemangleIt(argv[1]) << std::endl;
    return 0;
  } else {
    return RUN_ALL_TESTS();
  }
}
