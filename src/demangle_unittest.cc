// Copyright (c) 2006, Google Inc.
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
// Unit tests for functions in demangle.c.

#include "demangle.h"

#include <gtest/gtest.h>

#include <fstream>
#include <iostream>
#include <string>

#include "base/commandlineflags.h"
#include "config.h"
#include "ng-log/logging.h"
#include "testing_utilities.h"
#include "utilities.h"

#ifdef NGLOG_USE_GFLAGS
#  include <gflags/gflags.h>
using namespace GFLAGS_NAMESPACE;
#endif

NGLOG_DEFINE_bool(demangle_filter, false,
                  "Run demangle_unittest in filter mode");

using namespace std;
using namespace nglog;

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

#  if defined(HAVE_DBGHELP) && !defined(NDEBUG)
TEST(Demangle, Windows) {
  EXPECT_STREQ("public: static void __cdecl Foo::func(int)",
               DemangleIt("?func@Foo@@SAXH@Z"));
  EXPECT_STREQ("public: static void __cdecl Foo::func(int)",
               DemangleIt("@ILT+1105(?func@Foo@@SAXH@Z)"));
  EXPECT_STREQ("int __cdecl foobarArray(int * const)",
               DemangleIt("?foobarArray@@YAHQAH@Z"));
}
#  endif

#else

// Test corner cases of boundary conditions.
TEST(Demangle, CornerCases) {
  const size_t size = 10;
  char tmp[size] = {0};
  const char* demangled = "foobar()";
  const char* mangled = "_Z6foobarv";
  EXPECT_TRUE(Demangle(mangled, tmp, sizeof(tmp)));
  // sizeof("foobar()") == size - 1
  EXPECT_STREQ(demangled, tmp);
  EXPECT_TRUE(Demangle(mangled, tmp, size - 1));
  EXPECT_STREQ(demangled, tmp);
  EXPECT_FALSE(Demangle(mangled, tmp, size - 2));  // Not enough.
  EXPECT_FALSE(Demangle(mangled, tmp, 1));
  EXPECT_FALSE(Demangle(mangled, tmp, 0));
  EXPECT_FALSE(Demangle(mangled, nullptr, 0));  // Should not cause SEGV.
}

// Test handling of functions suffixed with .clone.N, which is used by GCC
// 4.5.x, and .constprop.N and .isra.N, which are used by GCC 4.6.x.  These
// suffixes are used to indicate functions which have been cloned during
// optimization.  We ignore these suffixes.
TEST(Demangle, Clones) {
  char tmp[20];
  EXPECT_TRUE(Demangle("_ZL3Foov", tmp, sizeof(tmp)));
  EXPECT_STREQ("Foo()", tmp);
  EXPECT_TRUE(Demangle("_ZL3Foov.clone.3", tmp, sizeof(tmp)));
  EXPECT_STREQ("Foo()", tmp);
  EXPECT_TRUE(Demangle("_ZL3Foov.constprop.80", tmp, sizeof(tmp)));
  EXPECT_STREQ("Foo()", tmp);
  EXPECT_TRUE(Demangle("_ZL3Foov.isra.18", tmp, sizeof(tmp)));
  EXPECT_STREQ("Foo()", tmp);
  EXPECT_TRUE(Demangle("_ZL3Foov.isra.2.constprop.18", tmp, sizeof(tmp)));
  EXPECT_STREQ("Foo()", tmp);
  // Invalid (truncated), should not demangle.
  EXPECT_FALSE(Demangle("_ZL3Foov.clo", tmp, sizeof(tmp)));
  // Invalid (.clone. not followed by number), should not demangle.
  EXPECT_FALSE(Demangle("_ZL3Foov.clone.", tmp, sizeof(tmp)));
  // Invalid (.clone. followed by non-number), should not demangle.
  EXPECT_FALSE(Demangle("_ZL3Foov.clone.foo", tmp, sizeof(tmp)));
  // Invalid (.constprop. not followed by number), should not demangle.
  EXPECT_FALSE(Demangle("_ZL3Foov.isra.2.constprop.", tmp, sizeof(tmp)));
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
    EXPECT_LT(strnlen(big, sizeof(big)), sizeof(big));
  }

  char small_buf[256];
#  if defined(HAVE___CXA_DEMANGLE)
  // abi::__cxa_demangle() produces the full name, including template
  // arguments, which cannot fit: this must fail rather than truncate.
  EXPECT_FALSE(Demangle(mangled, small_buf, sizeof(small_buf)));
#  else   // !defined(HAVE___CXA_DEMANGLE)
  // Other implementations may abbreviate (e.g. the fallback demangler
  // omits template arguments) and legitimately fit, but a reported
  // success must still be '\0'-terminated.
  if (Demangle(mangled, small_buf, sizeof(small_buf))) {
    EXPECT_LT(strnlen(small_buf, sizeof(small_buf)), sizeof(small_buf));
  }
#  endif  // defined(HAVE___CXA_DEMANGLE)
}

TEST(Demangle, FromFile) {
  string test_file = TestSrcDir() + "/src/demangle_unittest.txt";
  ifstream f(test_file.c_str());  // The file should exist.
  EXPECT_FALSE(f.fail());

  string line;
  while (getline(f, line)) {
    // Lines start with '#' are considered as comments.
    if (line.empty() || line[0] == '#') {
      continue;
    }
    // Each line should contain a mangled name and a demangled name
    // separated by '\t'.  Example: "_Z3foo\tfoo"
    string::size_type tab_pos = line.find('\t');
    EXPECT_NE(string::npos, tab_pos);
    string mangled = line.substr(0, tab_pos);
    string demangled = line.substr(tab_pos + 1);
    EXPECT_EQ(demangled, DemangleIt(mangled.c_str()));
  }
}

#endif

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
#ifdef NGLOG_USE_GFLAGS
  ParseCommandLineFlags(&argc, &argv, true);
#endif

  FLAGS_logtostderr = true;
  InitializeLogging(argv[0]);
  if (FLAGS_demangle_filter) {
    // Read from cin and write to cout.
    string line;
    while (getline(cin, line, '\n')) {
      cout << DemangleIt(line.c_str()) << endl;
    }
    return 0;
  } else if (argc > 1) {
    cout << DemangleIt(argv[1]) << endl;
    return 0;
  } else {
    return RUN_ALL_TESTS();
  }
}
