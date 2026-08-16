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
// An async-signal-safe and thread-safe demangler for the Itanium C++ ABI.

// The demangler is designed for use in async signal handlers to symbolize
// stack traces.  This requirement deliberately limits the parser.  It does
// not allocate memory and deliberately produces an abbreviated result to
// minimize consumption of the caller-provided output buffer.  The platform's
// complete demangler may allocate memory and cannot be used in such signal
// handlers.
//
// The parser accepts the Itanium name grammar used by current C++ compilers,
// including dependent expressions, constraints, modern builtin types, local
// entities, and special names.  The output remains deliberately abbreviated
// to minimize consumption of the caller-provided output buffer.
// It omits function parameter types and template argument types while
// retaining class, function, constructor, destructor, and operator names.
// Substitutions and template parameters are represented as "?".  Template
// argument lists are represented as "<>".  Expression operands are parsed
// for grammar validation but are not emitted.  These choices preserve the
// symbol's useful identity while avoiding output that can consume most of
// the caller-provided buffer.
// Numeric fields whose values are not needed are consumed without conversion,
// so arbitrarily long ABI numbers remain valid. Values needed for parsing
// decisions use checked fixed-width integers and are rejected when they do not
// fit. Recursive productions have both grammar-specific and cumulative depth
// limits. Inputs beyond those limits are rejected to bound stack use.
// These limitations keep the parser suitable for failure signal handlers
// without dynamic allocation. Demangle() always uses this parser on Itanium
// ABI platforms.
//
// See the implementation note in demangle.cc if you are interested.
//
// Example:
//
// | Mangled Name  | The Demangler | abi::__cxa_demangle()
// |---------------|---------------|-----------------------
// | _Z1fv         | f()           | f()
// | _Z1fi         | f()           | f(int)
// | _Z3foo3bar    | foo()         | foo(bar)
// | _Z1fIiEvi     | f<>()         | void f<int>(int)
// | _ZN1N1fE      | N::f          | N::f
// | _ZN3Foo3BarEv | Foo::Bar()    | Foo::Bar()
// | _Zrm1XS_      | operator%()   | operator%(X, X)
// | _ZN3FooC1Ev   | Foo::Foo()    | Foo::Foo()
// | _Z1fSs        | f()           | f(std::basic_string<char,
// |               |               |   std::char_traits<char>,
// |               |               |   std::allocator<char> >)
//
// See the unit test for more examples.
//
// DemangleWithSystem() is available for callers that need the platform's full
// spelling outside signal handlers.
//

#ifndef NGLOG_INTERNAL_DEMANGLE_H
#define NGLOG_INTERNAL_DEMANGLE_H

#include <cstddef>

#include "ng-log/export.h"

namespace nglog {
inline namespace tools {

// Demangle "mangled" with the local parser. On success, return true and write
// the abbreviated demangled symbol name to "out". Otherwise, return false.
// On failure, "out" is cleared when it is non-null and "out_size" is non-zero.
// The parser does not allocate memory and is suitable for use from failure
// signal handlers.
bool NGLOG_NO_EXPORT Demangle(const char* mangled, char* out,
                              std::size_t out_size);

// Demangle using the platform's complete demangler when one is available.
// This may allocate memory and must not be called from a signal handler.
bool NGLOG_NO_EXPORT DemangleWithSystem(const char* mangled, char* out,
                                        std::size_t out_size);

}  // namespace tools
}  // namespace nglog

#endif  // NGLOG_INTERNAL_DEMANGLE_H
