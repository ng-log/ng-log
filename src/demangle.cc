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
// For reference check out:
// https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling

#include "demangle.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>

#include "internal/character_classification.h"
#include "utilities.h"

#if defined(HAVE___CXA_DEMANGLE)
#  include <cxxabi.h>
#endif

#if defined(NGLOG_OS_WINDOWS)
// clang-format off
#  include <windows.h>  // Must come before <dbghelp.h>
#  include <dbghelp.h>
// clang-format on
#endif

namespace nglog {
inline namespace tools {

using internal::IsDecimalDigit;

#if !defined(_MSC_VER)
namespace {
enum class OperatorArity { kUnary, kBinary, kTernary, kOther };

struct AbbrevPair {
  const char* const abbrev;
  const char* const real_name;
};

struct OperatorPair {
  const char* const abbrev;
  const char* const real_name;
  const OperatorArity arity;
};

// List of operators from Itanium C++ ABI.
const OperatorPair kOperatorList[] = {
    {"nw", "new", OperatorArity::kUnary},
    {"na", "new[]", OperatorArity::kUnary},
    {"dl", "delete", OperatorArity::kUnary},
    {"da", "delete[]", OperatorArity::kUnary},
    {"ps", "+", OperatorArity::kUnary},
    {"ng", "-", OperatorArity::kUnary},
    {"ad", "&", OperatorArity::kUnary},
    {"de", "*", OperatorArity::kUnary},
    {"co", "~", OperatorArity::kUnary},
    {"pl", "+", OperatorArity::kBinary},
    {"mi", "-", OperatorArity::kBinary},
    {"ml", "*", OperatorArity::kBinary},
    {"dv", "/", OperatorArity::kBinary},
    {"rm", "%", OperatorArity::kBinary},
    {"an", "&", OperatorArity::kBinary},
    {"or", "|", OperatorArity::kBinary},
    {"eo", "^", OperatorArity::kBinary},
    {"aS", "=", OperatorArity::kBinary},
    {"pL", "+=", OperatorArity::kBinary},
    {"mI", "-=", OperatorArity::kBinary},
    {"mL", "*=", OperatorArity::kBinary},
    {"dV", "/=", OperatorArity::kBinary},
    {"rM", "%=", OperatorArity::kBinary},
    {"aN", "&=", OperatorArity::kBinary},
    {"oR", "|=", OperatorArity::kBinary},
    {"eO", "^=", OperatorArity::kBinary},
    {"ls", "<<", OperatorArity::kBinary},
    {"rs", ">>", OperatorArity::kBinary},
    {"lS", "<<=", OperatorArity::kBinary},
    {"rS", ">>=", OperatorArity::kBinary},
    {"eq", "==", OperatorArity::kBinary},
    {"ne", "!=", OperatorArity::kBinary},
    {"lt", "<", OperatorArity::kBinary},
    {"gt", ">", OperatorArity::kBinary},
    {"le", "<=", OperatorArity::kBinary},
    {"ge", ">=", OperatorArity::kBinary},
    {"nt", "!", OperatorArity::kUnary},
    {"aa", "&&", OperatorArity::kBinary},
    {"oo", "||", OperatorArity::kBinary},
    {"pp", "++", OperatorArity::kUnary},
    {"mm", "--", OperatorArity::kUnary},
    {"cm", ",", OperatorArity::kBinary},
    {"pm", "->*", OperatorArity::kBinary},
    {"pt", "->", OperatorArity::kBinary},
    {"cl", "()", OperatorArity::kBinary},
    {"ix", "[]", OperatorArity::kBinary},
    {"qu", "?", OperatorArity::kTernary},
    {"aw", "co_await", OperatorArity::kUnary},
    {"ss", "<=>", OperatorArity::kBinary},
    {"st", "sizeof", OperatorArity::kUnary},
    {"sz", "sizeof", OperatorArity::kUnary},
    {nullptr, nullptr, OperatorArity::kOther},
};

// List of builtin types from Itanium C++ ABI.
const AbbrevPair kBuiltinTypeList[] = {{"v", "void"},
                                       {"w", "wchar_t"},
                                       {"b", "bool"},
                                       {"c", "char"},
                                       {"a", "signed char"},
                                       {"h", "unsigned char"},
                                       {"s", "short"},
                                       {"t", "unsigned short"},
                                       {"i", "int"},
                                       {"j", "unsigned int"},
                                       {"l", "long"},
                                       {"m", "unsigned long"},
                                       {"x", "long long"},
                                       {"y", "unsigned long long"},
                                       {"n", "__int128"},
                                       {"o", "unsigned __int128"},
                                       {"f", "float"},
                                       {"d", "double"},
                                       {"e", "long double"},
                                       {"g", "__float128"},
                                       {"z", "ellipsis"},
                                       {"Da", "auto"},
                                       {"Dc", "decltype(auto)"},
                                       {"Dd", "decimal64"},
                                       {"De", "decimal128"},
                                       {"Df", "decimal32"},
                                       {"Dh", "float16"},
                                       {"Di", "char32_t"},
                                       {"Ds", "char16_t"},
                                       {"Du", "char8_t"},
                                       {"Dn", "decltype(nullptr)"},
                                       {nullptr, nullptr}};

// List of substitutions Itanium C++ ABI.
const AbbrevPair kSubstitutionList[] = {
    {"St", ""},
    {"Sa", "allocator"},
    {"Sb", "basic_string"},
    // std::basic_string<char, std::char_traits<char>,std::allocator<char> >
    {"Ss", "string"},
    // std::basic_istream<char, std::char_traits<char> >
    {"Si", "istream"},
    // std::basic_ostream<char, std::char_traits<char> >
    {"So", "ostream"},
    // std::basic_iostream<char, std::char_traits<char> >
    {"Sd", "iostream"},
    {nullptr, nullptr}};

// State needed for demangling.
struct State {
  const char* mangled_cur;   // Cursor of mangled name.
  char* out_cur;             // Cursor of output string.
  const char* out_begin;     // Beginning of output string.
  const char* out_end;       // End of output string.
  const char* prev_name;     // For constructors/destructors.
  ssize_t prev_name_length;  // For constructors/destructors.
  short nest_level;          // For nested names.
  bool append;               // Append flag.
  bool overflowed;           // True if output gets overflowed.
  bool skip_separator;
  std::uint32_t local_level;
  std::uint32_t expr_level;
  std::uint32_t arg_level;
  char pending_ref_qualifier;
  std::uint32_t encoding_level;
  std::uint32_t type_level;
  std::uint32_t parse_depth;
};

constexpr std::uint32_t kMaxParseDepth = 64;
constexpr std::uint32_t kMaxEncodingLevel = 64;
constexpr std::uint32_t kMaxTypeLevel = 64;
constexpr std::uint32_t kMaxTemplateArgumentLevel = 64;
constexpr std::uint32_t kMaxExpressionLevel = 64;
constexpr std::uint32_t kMaxLocalNameLevel = 64;

template <std::uint32_t State::* level, std::uint32_t max_level>
class ParseLevelGuard {
 public:
  explicit ParseLevelGuard(State* state) noexcept
      : level_((state->*level) < max_level ? &(state->*level) : nullptr) {
    if (level_ != nullptr) {
      ++*level_;
    }
  }

  ~ParseLevelGuard() {
    if (level_ != nullptr) {
      --*level_;
    }
  }

  ParseLevelGuard(const ParseLevelGuard&) = delete;
  ParseLevelGuard& operator=(const ParseLevelGuard&) = delete;
  ParseLevelGuard(ParseLevelGuard&&) = delete;
  ParseLevelGuard& operator=(ParseLevelGuard&&) = delete;

  explicit operator bool() const noexcept { return level_ != nullptr; }

 private:
  std::uint32_t* const level_;
};

using ParseDepthGuard = ParseLevelGuard<&State::parse_depth, kMaxParseDepth>;
using ParseEncodingGuard =
    ParseLevelGuard<&State::encoding_level, kMaxEncodingLevel>;
using ParseTypeGuard = ParseLevelGuard<&State::type_level, kMaxTypeLevel>;
using ParseTemplateArgGuard =
    ParseLevelGuard<&State::arg_level, kMaxTemplateArgumentLevel>;
using ParseExpressionGuard =
    ParseLevelGuard<&State::expr_level, kMaxExpressionLevel>;
using ParseLocalNameGuard =
    ParseLevelGuard<&State::local_level, kMaxLocalNameLevel>;

static_assert(!std::is_copy_constructible<ParseDepthGuard>::value,
              "parse guards must not be copyable");
static_assert(!std::is_move_constructible<ParseDepthGuard>::value,
              "parse guards must not be movable");

// Returns true if "str" has at least "n" characters remaining.
bool AtLeastNumCharsRemaining(const char* str, ssize_t n) {
  if (n <= 0) {
    return true;
  }
  return std::memchr(str, '\0', static_cast<std::size_t>(n)) == nullptr;
}

// Returns true if "str" has "prefix" as a prefix.
bool StrPrefix(const char* str, const char* prefix) {
  return std::strncmp(str, prefix, std::strlen(prefix)) == 0;
}

void InitState(State* state, const char* mangled, char* out,
               std::size_t out_size) {
  state->mangled_cur = mangled;
  state->out_cur = out;
  state->out_begin = out;
  state->out_end = out == nullptr ? nullptr : out + out_size;
  state->prev_name = nullptr;
  state->prev_name_length = -1;
  state->nest_level = -1;
  state->append = true;
  state->overflowed = false;
  state->skip_separator = false;
  state->local_level = 0;
  state->expr_level = 0;
  state->arg_level = 0;
  state->pending_ref_qualifier = '\0';
  state->encoding_level = 0;
  state->type_level = 0;
  state->parse_depth = 0;
}

// Returns true and advances "mangled_cur" if we find "one_char_token"
// at "mangled_cur" position.  It is assumed that "one_char_token" does
// not contain '\0'.
bool ParseOneCharToken(State* state, const char one_char_token) {
  if (state->mangled_cur[0] == one_char_token) {
    ++state->mangled_cur;
    return true;
  }
  return false;
}

// Returns true and advances "mangled_cur" if we find "two_char_token"
// at "mangled_cur" position.  It is assumed that "two_char_token" does
// not contain '\0'.
bool ParseTwoCharToken(State* state, const char* two_char_token) {
  if (std::strncmp(state->mangled_cur, two_char_token, 2) == 0) {
    state->mangled_cur += 2;
    return true;
  }
  return false;
}

// Returns true and advances "mangled_cur" if we find any character in
// "char_class" at "mangled_cur" position.
bool ParseCharClass(State* state, const char* char_class) {
  if (state->mangled_cur[0] != '\0' &&
      std::strchr(char_class, state->mangled_cur[0]) != nullptr) {
    ++state->mangled_cur;
    return true;
  }
  return false;
}

// This function is used for handling an optional non-terminal.
bool Optional(bool) { return true; }

// This function is used for handling <non-terminal>+ syntax.
using ParseFunc = bool (*)(State*);
bool OneOrMore(ParseFunc parse_func, State* state) {
  if (parse_func(state)) {
    while (parse_func(state)) {
    }
    return true;
  }
  return false;
}

// This function is used for handling <non-terminal>* syntax. The function
// always returns true and must be followed by a termination token or a
// terminating sequence not handled by parse_func (e.g.
// ParseOneCharToken(state, 'E')).
bool ZeroOrMore(ParseFunc parse_func, State* state) {
  while (parse_func(state)) {
  }
  return true;
}

// Append "str" at "out_cur".  If there is an overflow, "overflowed"
// is set to true for later use.  The output string is ensured to
// always terminate with '\0' as long as there is no overflow.
void Append(State* state, const char* const str, ssize_t length) {
  if (state->out_cur == nullptr) {
    state->overflowed = true;
    return;
  }
  for (ssize_t i = 0; i < length; ++i) {
    if (state->out_end - state->out_cur > 1) {  // +1 for '\0'
      *state->out_cur = str[i];
      ++state->out_cur;
    } else {
      state->overflowed = true;
      break;
    }
  }
  if (!state->overflowed) {
    *state->out_cur = '\0';  // Terminate it with '\0'
  }
}

// Returns true if "str" is a function clone suffix.  These suffixes are used
// by GCC 4.5.x and later versions to indicate functions which have been
// cloned during optimization.  We treat any sequence (.<alpha>+.<digit>+)+ as
// a function clone suffix.
bool IsFunctionCloneSuffix(const char* str) {
  std::size_t i = 0;
  while (str[i] != '\0') {
    // Consume a single .<alpha>+.<digit>+ sequence.
    if (str[i] != '.' || !internal::IsAlpha(str[i + 1])) {
      return false;
    }
    i += 2;
    while (internal::IsAlpha(str[i])) {
      ++i;
    }
    if (str[i] != '.' || !internal::IsDecimalDigit(str[i + 1])) {
      return false;
    }
    i += 2;
    while (internal::IsDecimalDigit(str[i])) {
      ++i;
    }
  }
  return true;  // Consumed everything in "str".
}

// Append "str" with some tweaks, iff "append" state is true.
// Returns true so that it can be placed in "if" conditions.
void MaybeAppendWithLength(State* state, const char* const str,
                           ssize_t length) {
  if (state->append && length > 0) {
    // Append a space if the output buffer ends with '<' and "str"
    // starts with '<' to avoid <<<.
    if (str[0] == '<' && state->out_begin < state->out_cur &&
        state->out_cur[-1] == '<') {
      Append(state, " ", 1);
    }
    char* const out_cur_before_append = state->out_cur;
    Append(state, str, length);
    // Remember the last identifier name for ctors/dtors, but only once we
    // know it was fully written to the output buffer. Otherwise
    // "prev_name" would point at output buffer bytes that were never
    // initialized by the demangler, which get read back later when
    // reconstructing constructor/destructor names.
    if (!state->overflowed && (internal::IsAlpha(str[0]) || str[0] == '_')) {
      state->prev_name = out_cur_before_append;
      state->prev_name_length = length;
    }
  }
}

// A convenient wrapper around MaybeAppendWithLength().
bool MaybeAppend(State* state, const char* const str) {
  if (state->append) {
    const std::size_t length = std::strlen(str);
    MaybeAppendWithLength(state, str, static_cast<ssize_t>(length));
  }
  return true;
}

// This function is used for handling nested names.
bool EnterNestedName(State* state) {
  state->nest_level = 0;
  return true;
}

// This function is used for handling nested names.
bool LeaveNestedName(State* state, short prev_value) {
  state->nest_level = prev_value;
  return true;
}

// Disable the append mode not to print function parameters, etc.
bool DisableAppend(State* state) {
  state->append = false;
  return true;
}

// Restore the append mode to the previous state.
bool RestoreAppend(State* state, bool prev_value) {
  state->append = prev_value;
  return true;
}

// Increase the nest level for nested names.
void MaybeIncreaseNestLevel(State* state) {
  if (state->nest_level > -1) {
    ++state->nest_level;
  }
}

// Appends :: for nested names if necessary.
void MaybeAppendSeparator(State* state) {
  if (state->skip_separator) {
    state->skip_separator = false;
    return;
  }
  if (state->nest_level >= 1) {
    MaybeAppend(state, "::");
  }
}

// Cancel the last separator if necessary.
void MaybeCancelLastSeparator(State* state) {
  if (state->nest_level >= 1 && state->append && state->out_begin != nullptr &&
      state->out_cur != nullptr && state->out_cur - state->out_begin >= 2) {
    state->out_cur -= 2;
    *state->out_cur = '\0';
  }
}

// Returns true if the identifier of the given length pointed to by
// "mangled_cur" is anonymous namespace.
bool IdentifierIsAnonymousNamespace(State* state, ssize_t length) {
  const char anon_prefix[] = "_GLOBAL__N_";
  return (length > static_cast<ssize_t>(sizeof(anon_prefix)) -
                       1 &&  // Should be longer.
          StrPrefix(state->mangled_cur, anon_prefix));
}

// Forward declarations of our parsing functions.
bool ParseMangledName(State* state);
bool ParseEncoding(State* state);
bool ParseName(State* state);
bool ParseUnscopedName(State* state);
bool ParseUnscopedTemplateName(State* state);
bool ParseNestedName(State* state);
bool ParsePrefix(State* state);
bool ParseUnqualifiedName(State* state);
bool ParseUnnamedTypeName(State* state);
bool ParseStructuredBindingName(State* state);
bool ParseSourceName(State* state);
bool ParseLocalSourceName(State* state);
bool ParseNumber(State* state, std::int64_t* number_out);
bool ParseNonNegativeNumber(State* state, std::int64_t* number_out);
bool ParsePositiveNumber(State* state, std::int64_t* number_out);
bool ParseFloatNumber(State* state);
bool ParseSeqId(State* state);
bool ParseIdentifier(State* state, ssize_t length);
bool ParseAbiTags(State* state);
bool ParseAbiTag(State* state);
bool ParseOperatorName(State* state);
bool ParseSpecialName(State* state);
bool ParseRequiresClause(State* state);
bool ParseCallOffset(State* state);
bool ParseNVOffset(State* state);
bool ParseVOffset(State* state);
bool ParseCtorDtorName(State* state);
bool ParseType(State* state);
bool ParseCVQualifiers(State* state);
bool ParseBuiltinType(State* state);
bool ParseFunctionType(State* state);
bool ParseExceptionSpec(State* state);
bool ParseBareFunctionType(State* state);
bool ParseClassEnumType(State* state);
bool ParseArrayType(State* state);
bool ParsePointerToMemberType(State* state);
bool ParseTemplateParam(State* state);
bool ParseTemplateTemplateParam(State* state);
bool ParseTemplateArgs(State* state);
bool ParseTemplateArg(State* state);
bool ParseFunctionParam(State* state);
bool ParseExpression(State* state);
bool ParseExprPrimary(State* state);
bool ParseUnresolvedName(State* state);
bool ParseBracedExpression(State* state);
bool ParseExpressionSequence(State* state, char terminator, bool require_one);
bool ParseBracedExpressionSequence(State* state);
bool ParseInitializer(State* state);
bool ParseRequiresExpression(State* state);
bool ParseLocalName(State* state);
bool ParseDiscriminator(State* state);
bool ParseSubstitution(State* state);

// Implementation note: the following code is a straightforward
// translation of the Itanium C++ ABI defined in BNF with a couple of
// exceptions.
//
// - Support GNU extensions not defined in the Itanium C++ ABI
// - <prefix> and <template-prefix> are combined to avoid infinite loop
// - Reorder patterns to shorten the code
// - Reorder patterns to give greedier functions precedence
//   We'll mark "Less greedy than" for these cases in the code
//
// Each parsing function changes the state and returns true on
// success.  Otherwise, don't change the state and returns false.  To
// ensure that the state isn't changed in the latter case, we save the
// original state before we call more than one parsing functions
// consecutively with &&, and restore the state if unsuccessful.  See
// ParseEncoding() as an example of this convention.  We follow the
// convention throughout the code.
//
// Following the full grammar is necessary for nested template arguments and
// dependent expressions.
//
// Note that (foo) in <(foo) ...> is a modifier to be ignored.
//
// The grammar follows the Itanium C++ ABI mangling specification.

// <mangled-name> ::= _Z <encoding>
bool ParseMangledName(State* state) {
  return ParseTwoCharToken(state, "_Z") && ParseEncoding(state);
}

// <encoding> ::= <(function) name> <bare-function-type>
//            ::= <(data) name>
//            ::= <special-name>
bool ParseEncoding(State* state) {
  ParseDepthGuard depth(state);
  if (!depth) {
    return false;
  }
  ParseEncodingGuard level(state);
  if (!level) {
    return false;
  }
  State copy = *state;
  if (ParseName(state) && Optional(ParseRequiresClause(state)) &&
      ParseBareFunctionType(state)) {
    if (state->pending_ref_qualifier == 'R') {
      MaybeAppend(state, " &");
    } else if (state->pending_ref_qualifier == 'O') {
      MaybeAppend(state, " &&");
    }
    state->pending_ref_qualifier = '\0';
    if (Optional(ParseRequiresClause(state))) {
      return true;
    }
  }
  *state = copy;

  if (ParseName(state) || ParseSpecialName(state)) {
    return true;
  }
  *state = copy;
  return false;
}

// <name> ::= <nested-name>
//        ::= <unscoped-template-name> <template-args>
//        ::= <unscoped-name>
//        ::= <local-name>
bool ParseName(State* state) {
  ParseDepthGuard depth(state);
  if (!depth) {
    return false;
  }
  if (ParseNestedName(state) || ParseLocalName(state)) {
    return true;
  }

  State copy = *state;
  if (ParseUnscopedTemplateName(state) && ParseTemplateArgs(state)) {
    return true;
  }
  *state = copy;

  // Less greedy than <unscoped-template-name> <template-args>.
  if (ParseUnscopedName(state)) {
    return true;
  }
  return false;
}

// <unscoped-name> ::= <unqualified-name>
//                 ::= St <unqualified-name>
bool ParseUnscopedName(State* state) {
  if (ParseUnqualifiedName(state)) {
    return true;
  }

  State copy = *state;
  if (ParseTwoCharToken(state, "St") && MaybeAppend(state, "std::") &&
      ParseUnqualifiedName(state)) {
    return true;
  }
  *state = copy;
  return false;
}

// <unscoped-template-name> ::= <unscoped-name>
//                          ::= <substitution>
bool ParseUnscopedTemplateName(State* state) {
  return ParseUnscopedName(state) || ParseSubstitution(state);
}

// <nested-name> ::= N [<CV-qualifiers>] <prefix> <unqualified-name> E
//               ::= N [<CV-qualifiers>] <template-prefix> <template-args> E
bool ParseNestedName(State* state) {
  ParseDepthGuard depth(state);
  if (!depth) {
    return false;
  }
  State copy = *state;
  if (ParseOneCharToken(state, 'N') && EnterNestedName(state) &&
      Optional(ParseCVQualifiers(state))) {
    char ref_qualifier = '\0';
    if (state->mangled_cur[0] == 'R' || state->mangled_cur[0] == 'O') {
      ref_qualifier = state->mangled_cur[0];
      ++state->mangled_cur;
    }
    if (ParsePrefix(state)) {
      state->pending_ref_qualifier = ref_qualifier;
      if (LeaveNestedName(state, copy.nest_level) &&
          ParseOneCharToken(state, 'E')) {
        return true;
      }
    }
  }
  *state = copy;
  return false;
}

// This part is tricky.  If we literally translate them to code, we'll
// end up infinite loop.  Hence we merge them to avoid the case.
//
// <prefix> ::= <prefix> <unqualified-name>
//          ::= <template-prefix> <template-args>
//          ::= <template-param>
//          ::= <substitution>
//          ::= # empty
// <template-prefix> ::= <prefix> <(template) unqualified-name>
//                   ::= <template-param>
//                   ::= <substitution>
bool ParsePrefix(State* state) {
  ParseDepthGuard depth(state);
  if (!depth) {
    return false;
  }
  bool has_something = false;
  while (true) {
    MaybeAppendSeparator(state);
    if (ParseTemplateParam(state) || ParseSubstitution(state) ||
        ParseUnscopedName(state)) {
      has_something = true;
      MaybeIncreaseNestLevel(state);
      continue;
    }
    MaybeCancelLastSeparator(state);
    if (has_something && ParseTemplateArgs(state)) {
      continue;
    }
    State copy = *state;
    if (ParseOneCharToken(state, 'M')) {
      has_something = true;
      continue;
    }
    *state = copy;
    if (ParseOneCharToken(state, 'D') &&
        (ParseOneCharToken(state, 't') || ParseOneCharToken(state, 'T')) &&
        ParseExpression(state) && ParseOneCharToken(state, 'E')) {
      has_something = true;
      continue;
    }
    *state = copy;
    break;
  }
  return true;
}

// <unqualified-name> ::= <operator-name>
//                    ::= <ctor-dtor-name>
//                    ::= <source-name> [<abi-tags>]
//                    ::= <local-source-name> [<abi-tags>]
bool ParseUnqualifiedName(State* state) {
  State copy = *state;
  if (ParseOperatorName(state) && Optional(ParseAbiTags(state))) {
    return true;
  }
  *state = copy;
  if (ParseCtorDtorName(state)) {
    return true;
  }
  *state = copy;
  if (((state->mangled_cur[0] == 'U' &&
        (state->mangled_cur[1] == 'l' || state->mangled_cur[1] == 't')) &&
       ParseUnnamedTypeName(state)) ||
      (state->mangled_cur[0] == 'D' && state->mangled_cur[1] == 'C' &&
       ParseStructuredBindingName(state)) ||
      (ParseSourceName(state) && Optional(ParseAbiTags(state))) ||
      (ParseLocalSourceName(state) && Optional(ParseAbiTags(state)))) {
    return true;
  }
  *state = copy;
  return false;
}

// <source-name> ::= <positive length number> <identifier>
bool ParseSourceName(State* state) {
  State copy = *state;
  std::int64_t length = -1;
  if (ParsePositiveNumber(state, &length) &&
      length <= std::numeric_limits<ssize_t>::max() &&
      ParseIdentifier(state, static_cast<ssize_t>(length))) {
    return true;
  }
  *state = copy;
  return false;
}

// <unnamed-type-name> ::= Ut [<number>] _
//                       ::= Ul <lambda-sig> E [<number>] _
bool ParseUnnamedTypeName(State* state) {
  State copy = *state;
  if (ParseTwoCharToken(state, "Ut") &&
      Optional(ParseNonNegativeNumber(state, nullptr)) &&
      ParseOneCharToken(state, '_')) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "Ul")) {
    const bool previous_append = state->append;
    DisableAppend(state);
    if (ParseType(state) && ZeroOrMore(ParseType, state) &&
        ParseOneCharToken(state, 'E') &&
        Optional(ParseNonNegativeNumber(state, nullptr)) &&
        ParseOneCharToken(state, '_')) {
      RestoreAppend(state, previous_append);
      state->skip_separator = true;
      return true;
    }
  }
  *state = copy;
  return false;
}

// <unqualified-name> ::= DC <source-name>+ E
bool ParseStructuredBindingName(State* state) {
  State copy = *state;
  if (!ParseTwoCharToken(state, "DC")) {
    return false;
  }

  MaybeAppend(state, "[");
  if (!ParseSourceName(state)) {
    *state = copy;
    return false;
  }
  while (state->mangled_cur[0] != 'E') {
    State item = *state;
    MaybeAppend(state, ", ");
    if (!ParseSourceName(state)) {
      *state = item;
      break;
    }
  }
  if (!ParseOneCharToken(state, 'E')) {
    *state = copy;
    return false;
  }
  MaybeAppend(state, "]");
  return true;
}

// <local-source-name> ::= L <source-name> [<discriminator>]
bool ParseLocalSourceName(State* state) {
  State copy = *state;
  if (ParseOneCharToken(state, 'L') && ParseSourceName(state) &&
      Optional(ParseDiscriminator(state))) {
    return true;
  }
  *state = copy;
  return false;
}

// <number> ::= [n] <non-negative decimal integer>
// If "number_out" is non-null, then *number_out is set to the value of the
// parsed number on success.
bool ParseNumber(State* state, std::int64_t* number_out) {
  State copy = *state;
  const bool negative = ParseOneCharToken(state, 'n');
  const char* p = state->mangled_cur;
  if (*p == '0' && internal::IsDecimalDigit(p[1])) {
    *state = copy;
    return false;
  }

  // Most grammar productions only need to validate and consume a number.
  // Avoid imposing an artificial integer limit when its value is discarded.
  if (number_out == nullptr) {
    while (IsDecimalDigit(*p)) {
      ++p;
    }
    if (p != state->mangled_cur) {
      state->mangled_cur = p;
      return true;
    }
    *state = copy;
    return false;
  }

  std::uint64_t number = 0;
  constexpr std::uint64_t kMaxPositive =
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  constexpr std::uint64_t kMaxNegative = kMaxPositive + 1;
  const std::uint64_t limit = negative ? kMaxNegative : kMaxPositive;
  for (; *p != '\0'; ++p) {
    if (IsDecimalDigit(*p)) {
      const std::uint64_t digit = static_cast<std::uint64_t>(*p - '0');
      if (number > limit / 10 || (number == limit / 10 && digit > limit % 10)) {
        *state = copy;
        return false;
      }
      number = number * 10 + digit;
    } else {
      break;
    }
  }
  if (p != state->mangled_cur) {  // Conversion succeeded.
    state->mangled_cur = p;
    if (number_out != nullptr) {
      if (negative) {
        if (number == kMaxNegative) {
          *number_out = std::numeric_limits<std::int64_t>::min();
        } else {
          *number_out = -static_cast<std::int64_t>(number);
        }
      } else {
        *number_out = static_cast<std::int64_t>(number);
      }
    }
    return true;
  }
  return false;
}

bool ParseNonNegativeNumber(State* state, std::int64_t* number_out) {
  if (number_out == nullptr) {
    if (state->mangled_cur[0] == 'n') {
      return false;
    }
    return ParseNumber(state, nullptr);
  }

  State copy = *state;
  std::int64_t number = 0;
  if (ParseNumber(state, &number) && number >= 0) {
    if (number_out != nullptr) {
      *number_out = number;
    }
    return true;
  }
  *state = copy;
  return false;
}

bool ParsePositiveNumber(State* state, std::int64_t* number_out) {
  if (number_out == nullptr) {
    if (state->mangled_cur[0] == '0' || state->mangled_cur[0] == 'n') {
      return false;
    }
    return ParseNumber(state, nullptr);
  }

  State copy = *state;
  std::int64_t number = 0;
  if (ParseNonNegativeNumber(state, &number) && number > 0) {
    if (number_out != nullptr) {
      *number_out = number;
    }
    return true;
  }
  *state = copy;
  return false;
}

// Floating-point literals are encoded using a fixed-length lowercase
// hexadecimal string.
bool ParseFloatNumber(State* state) {
  const char* p = state->mangled_cur;
  for (; *p != '\0'; ++p) {
    if (!internal::IsLowerHexDigit(*p)) {
      break;
    }
  }
  if (p != state->mangled_cur) {  // Conversion succeeded.
    state->mangled_cur = p;
    return true;
  }
  return false;
}

// The <seq-id> is a sequence number in base 36,
// using digits and upper case letters
bool ParseSeqId(State* state) {
  const char* p = state->mangled_cur;
  const char* const first = p;
  for (; *p != '\0'; ++p) {
    if (!internal::IsDecimalDigit(*p) && !internal::IsUpper(*p)) {
      break;
    }
  }
  if (p != state->mangled_cur &&
      !(p - first > 1 && first[0] == '0')) {  // Conversion succeeded.
    state->mangled_cur = p;
    return true;
  }
  return false;
}

// <identifier> ::= <unqualified source code identifier> (of given length)
bool ParseIdentifier(State* state, ssize_t length) {
  if (length < 0 || !AtLeastNumCharsRemaining(state->mangled_cur, length)) {
    return false;
  }
  if (IdentifierIsAnonymousNamespace(state, length)) {
    MaybeAppend(state, "(anonymous namespace)");
  } else {
    MaybeAppendWithLength(state, state->mangled_cur, length);
  }
  state->mangled_cur += length;
  return true;
}

// <abi-tags> ::= <abi-tag> [<abi-tags>]
bool ParseAbiTags(State* state) {
  State copy = *state;
  DisableAppend(state);
  if (OneOrMore(ParseAbiTag, state)) {
    RestoreAppend(state, copy.append);
    return true;
  }
  *state = copy;
  return false;
}

// <abi-tag> ::= B <source-name>
bool ParseAbiTag(State* state) {
  return ParseOneCharToken(state, 'B') && ParseSourceName(state);
}

// <operator-name> ::= nw, and other two letters cases
//                 ::= cv <type>  # (cast)
//                 ::= v  <digit> <source-name> # vendor extended operator
bool ParseOperatorName(State* state) {
  if (!AtLeastNumCharsRemaining(state->mangled_cur, 2)) {
    return false;
  }
  // First check with "cv" (cast) case.
  State copy = *state;
  if (ParseTwoCharToken(state, "cv") && MaybeAppend(state, "operator ") &&
      EnterNestedName(state) && ParseType(state) &&
      LeaveNestedName(state, copy.nest_level)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "li") && MaybeAppend(state, "operator\"\" ") &&
      ParseSourceName(state)) {
    return true;
  }
  *state = copy;

  // Then vendor extended operators.
  if (ParseOneCharToken(state, 'v') && ParseCharClass(state, "0123456789") &&
      ParseSourceName(state)) {
    return true;
  }
  *state = copy;

  // Other operator names should start with a lower alphabet followed
  // by a lower/upper alphabet.
  if (!(internal::IsLower(state->mangled_cur[0]) &&
        internal::IsAlpha(state->mangled_cur[1]))) {
    return false;
  }
  // We may want to perform a binary search if we really need speed.
  const OperatorPair* p;
  for (p = kOperatorList; p->abbrev != nullptr; ++p) {
    if (state->mangled_cur[0] == p->abbrev[0] &&
        state->mangled_cur[1] == p->abbrev[1]) {
      MaybeAppend(state, "operator");
      if (internal::IsLower(*p->real_name)) {  // new, delete, etc.
        MaybeAppend(state, " ");
      }
      MaybeAppend(state, p->real_name);
      state->mangled_cur += 2;
      return true;
    }
  }
  return false;
}

bool OperatorHasArity(const char* const op, const OperatorArity arity) {
  if (StrPrefix(op, "cv") || StrPrefix(op, "li") ||
      (op[0] == 'v' && AtLeastNumCharsRemaining(op, 2) &&
       IsDecimalDigit(op[1]))) {
    return arity == OperatorArity::kUnary;
  }

  for (const OperatorPair* p = kOperatorList; p->abbrev != nullptr; ++p) {
    if (op[0] == p->abbrev[0] && op[1] == p->abbrev[1]) {
      return p->arity == arity;
    }
  }
  return false;
}

bool ParseOperatorNameWithArity(State* state, const OperatorArity arity) {
  if (!OperatorHasArity(state->mangled_cur, arity)) {
    return false;
  }
  return ParseOperatorName(state);
}

// <special-name> ::= TV <type>
//                ::= TT <type>
//                ::= TI <type>
//                ::= TS <type>
//                ::= Tc <call-offset> <call-offset> <(base) encoding>
//                ::= GV <(object) name>
//                ::= T <call-offset> <(base) encoding>
// G++ extensions:
//                ::= TC <type> <(offset) number> _ <(base) type>
//                ::= TF <type>
//                ::= TJ <type>
//                ::= GR <name>
//                ::= GA <encoding>
//                ::= Th <call-offset> <(base) encoding>
//                ::= Tv <call-offset> <(base) encoding>
//
// Note: these names do not usually appear in stack traces.
bool ParseSpecialName(State* state) {
  State copy = *state;
  if (ParseTwoCharToken(state, "GV") && ParseName(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "GR") && ParseName(state) &&
      Optional(ParseSeqId(state)) && ParseOneCharToken(state, '_')) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "GT") && ParseOneCharToken(state, 't') &&
      MaybeAppend(state, "transaction clone for ") && ParseEncoding(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "TW") &&
      MaybeAppend(state, "TLS wrapper function for ") && ParseName(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "TH") &&
      MaybeAppend(state, "TLS init function for ") && ParseName(state)) {
    return true;
  }
  *state = copy;

  if (ParseOneCharToken(state, 'T') && ParseCharClass(state, "VTIS") &&
      ParseType(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "Tc") && ParseCallOffset(state) &&
      ParseCallOffset(state) && ParseEncoding(state)) {
    return true;
  }
  *state = copy;

  if (ParseOneCharToken(state, 'T') && ParseCallOffset(state) &&
      ParseEncoding(state)) {
    return true;
  }
  *state = copy;

  // G++ extensions
  if (ParseTwoCharToken(state, "TC") && ParseType(state) &&
      ParseNonNegativeNumber(state, nullptr) && ParseOneCharToken(state, '_') &&
      DisableAppend(state) && ParseType(state)) {
    RestoreAppend(state, copy.append);
    return true;
  }
  *state = copy;

  if (ParseOneCharToken(state, 'T') && ParseCharClass(state, "FJ") &&
      ParseType(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "GA") && ParseEncoding(state)) {
    return true;
  }
  *state = copy;

  if (ParseOneCharToken(state, 'T') && ParseCharClass(state, "hv") &&
      ParseCallOffset(state) && ParseEncoding(state)) {
    return true;
  }
  *state = copy;
  return false;
}

// <call-offset> ::= h <nv-offset> _
//               ::= v <v-offset> _
bool ParseCallOffset(State* state) {
  State copy = *state;
  if (ParseOneCharToken(state, 'h') && ParseNVOffset(state) &&
      ParseOneCharToken(state, '_')) {
    return true;
  }
  *state = copy;

  if (ParseOneCharToken(state, 'v') && ParseVOffset(state) &&
      ParseOneCharToken(state, '_')) {
    return true;
  }
  *state = copy;

  return false;
}

// <nv-offset> ::= <(offset) number>
bool ParseNVOffset(State* state) { return ParseNumber(state, nullptr); }

// <v-offset>  ::= <(offset) number> _ <(virtual offset) number>
bool ParseVOffset(State* state) {
  State copy = *state;
  if (ParseNumber(state, nullptr) && ParseOneCharToken(state, '_') &&
      ParseNumber(state, nullptr)) {
    return true;
  }
  *state = copy;
  return false;
}

// <ctor-dtor-name> ::= C1 | C2 | C3
//                  ::= D0 | D1 | D2
bool ParseCtorDtorName(State* state) {
  State copy = *state;
  if (ParseTwoCharToken(state, "CI") && ParseCharClass(state, "12") &&
      state->prev_name != nullptr && ParseType(state)) {
    return true;
  }
  *state = copy;

  if (ParseOneCharToken(state, 'C') && ParseCharClass(state, "123") &&
      state->prev_name != nullptr) {
    const char* const prev_name = state->prev_name;
    const ssize_t prev_name_length = state->prev_name_length;
    MaybeAppendWithLength(state, prev_name, prev_name_length);
    return true;
  }
  *state = copy;

  if (ParseOneCharToken(state, 'D') && ParseCharClass(state, "012") &&
      state->prev_name != nullptr) {
    const char* const prev_name = state->prev_name;
    const ssize_t prev_name_length = state->prev_name_length;
    MaybeAppend(state, "~");
    MaybeAppendWithLength(state, prev_name, prev_name_length);
    return true;
  }
  *state = copy;
  return false;
}

// <type> ::= <CV-qualifiers> <type>
//        ::= P <type>   # pointer-to
//        ::= R <type>   # reference-to
//        ::= O <type>   # rvalue reference-to (C++0x)
//        ::= C <type>   # complex pair (C 2000)
//        ::= G <type>   # imaginary (C 2000)
//        ::= U <source-name> <type>  # vendor extended type qualifier
//        ::= <builtin-type>
//        ::= <function-type>
//        ::= <class-enum-type>
//        ::= <array-type>
//        ::= <pointer-to-member-type>
//        ::= <template-template-param> <template-args>
//        ::= <template-param>
//        ::= <substitution>
//        ::= Dp <type>          # pack expansion of (C++0x)
//        ::= Dt <expression> E  # decltype of an id-expression or class
//                               # member access (C++0x)
//        ::= DT <expression> E  # decltype of an expression (C++0x)
//
bool ParseType(State* state) {
  ParseDepthGuard depth(state);
  if (!depth) {
    return false;
  }
  ParseTypeGuard level(state);
  if (!level) {
    return false;
  }
  // We should check CV-qualifers, and PRGC things first.
  State copy = *state;
  if (ParseCVQualifiers(state) && ParseType(state)) {
    return true;
  }
  *state = copy;

  if (ParseCharClass(state, "OPRCG") && ParseType(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "Dp") && ParseType(state)) {
    return true;
  }
  *state = copy;

  if ((ParseTwoCharToken(state, "Ts") || ParseTwoCharToken(state, "Tu") ||
       ParseTwoCharToken(state, "Te")) &&
      ParseName(state)) {
    return true;
  }
  *state = copy;

  if (ParseOneCharToken(state, 'D') && ParseCharClass(state, "tT") &&
      ParseExpression(state) && ParseOneCharToken(state, 'E')) {
    return true;
  }
  *state = copy;

  if (ParseOneCharToken(state, 'U') && ParseSourceName(state) &&
      Optional(ParseTemplateArgs(state)) && ParseType(state)) {
    return true;
  }
  *state = copy;

  if (ParseBuiltinType(state) || ParseFunctionType(state) ||
      ParseClassEnumType(state) || ParseArrayType(state) ||
      ParsePointerToMemberType(state) || ParseSubstitution(state)) {
    return true;
  }

  if (ParseTemplateTemplateParam(state) && ParseTemplateArgs(state)) {
    return true;
  }
  *state = copy;

  // Less greedy than <template-template-param> <template-args>.
  if (ParseTemplateParam(state)) {
    return true;
  }

  *state = copy;
  return false;
}

// <CV-qualifiers> ::= [r] [V] [K]
// We don't allow empty <CV-qualifiers> to avoid infinite loop in
// ParseType().
bool ParseCVQualifiers(State* state) {
  int num_cv_qualifiers = 0;
  num_cv_qualifiers += ParseOneCharToken(state, 'r');
  num_cv_qualifiers += ParseOneCharToken(state, 'V');
  num_cv_qualifiers += ParseOneCharToken(state, 'K');
  return num_cv_qualifiers > 0;
}

// <builtin-type> ::= v, etc.
//                ::= u <source-name>
bool ParseBuiltinType(State* state) {
  State original = *state;
  if (ParseTwoCharToken(state, "DF") &&
      ParseNonNegativeNumber(state, nullptr) &&
      (ParseOneCharToken(state, '_') || ParseOneCharToken(state, 'x') ||
       ParseOneCharToken(state, 'b'))) {
    return true;
  }
  *state = original;

  Optional(ParseTwoCharToken(state, "DS"));
  if ((ParseTwoCharToken(state, "DA") || ParseTwoCharToken(state, "DR")) &&
      ParseCharClass(state, "stijlm")) {
    return true;
  }
  *state = original;

  if (ParseTwoCharToken(state, "DB") || ParseTwoCharToken(state, "DU")) {
    State width = *state;
    if (ParseNonNegativeNumber(state, nullptr) &&
        ParseOneCharToken(state, '_')) {
      return true;
    }
    *state = width;
    if (ParseExpression(state) && ParseOneCharToken(state, '_')) {
      return true;
    }
  }
  *state = original;

  const AbbrevPair* p;
  for (p = kBuiltinTypeList; p->abbrev != nullptr; ++p) {
    if (StrPrefix(state->mangled_cur, p->abbrev)) {
      MaybeAppend(state, p->real_name);
      state->mangled_cur += std::strlen(p->abbrev);
      return true;
    }
  }

  State copy = *state;
  if (ParseOneCharToken(state, 'u') && ParseSourceName(state) &&
      Optional(ParseTemplateArgs(state))) {
    return true;
  }
  *state = copy;
  return false;
}

// <function-type> ::= F [Y] <bare-function-type> E
bool ParseFunctionType(State* state) {
  State copy = *state;
  if (Optional(ParseCVQualifiers(state)) &&
      Optional(ParseExceptionSpec(state)) &&
      Optional(ParseTwoCharToken(state, "Dx")) &&
      ParseOneCharToken(state, 'F') &&
      Optional(ParseOneCharToken(state, 'Y')) && ParseBareFunctionType(state) &&
      Optional(ParseCharClass(state, "RO")) && ParseOneCharToken(state, 'E')) {
    return true;
  }
  *state = copy;
  return false;
}

// <exception-spec> ::= Do
//                    ::= DO <expression> E
//                    ::= Dw <type>+ E
bool ParseExceptionSpec(State* state) {
  State copy = *state;
  DisableAppend(state);
  if (ParseTwoCharToken(state, "Do")) {
    RestoreAppend(state, copy.append);
    return true;
  }
  *state = copy;

  DisableAppend(state);
  if (ParseTwoCharToken(state, "DO") && ParseExpression(state) &&
      ParseOneCharToken(state, 'E')) {
    RestoreAppend(state, copy.append);
    return true;
  }
  *state = copy;

  DisableAppend(state);
  if (ParseTwoCharToken(state, "Dw") && OneOrMore(ParseType, state) &&
      ParseOneCharToken(state, 'E')) {
    RestoreAppend(state, copy.append);
    return true;
  }
  *state = copy;
  return false;
}

// <bare-function-type> ::= <(signature) type>+
bool ParseBareFunctionType(State* state) {
  State copy = *state;
  DisableAppend(state);
  if (OneOrMore(ParseType, state)) {
    RestoreAppend(state, copy.append);
    MaybeAppend(state, "()");
    return true;
  }
  *state = copy;
  return false;
}

// <requires-clause> ::= Q <constraint-expression>
bool ParseRequiresClause(State* state) {
  State copy = *state;
  DisableAppend(state);
  if (ParseOneCharToken(state, 'Q') && ParseExpression(state)) {
    RestoreAppend(state, copy.append);
    return true;
  }
  *state = copy;
  return false;
}

// <class-enum-type> ::= <name>
bool ParseClassEnumType(State* state) { return ParseName(state); }

// <array-type> ::= A <(positive dimension) number> _ <(element) type>
//              ::= A [<(dimension) expression>] _ <(element) type>
bool ParseArrayType(State* state) {
  State copy = *state;
  if (ParseOneCharToken(state, 'A') && ParsePositiveNumber(state, nullptr) &&
      ParseOneCharToken(state, '_') && ParseType(state)) {
    return true;
  }
  *state = copy;

  if (ParseOneCharToken(state, 'A') && Optional(ParseExpression(state)) &&
      ParseOneCharToken(state, '_') && ParseType(state)) {
    return true;
  }
  *state = copy;
  return false;
}

// <pointer-to-member-type> ::= M <(class) type> <(member) type>
bool ParsePointerToMemberType(State* state) {
  State copy = *state;
  if (ParseOneCharToken(state, 'M') && ParseType(state) && ParseType(state)) {
    return true;
  }
  *state = copy;
  return false;
}

// <template-param> ::= T_
//                  ::= T <parameter-2 non-negative number> _
bool ParseTemplateParam(State* state) {
  if (ParseTwoCharToken(state, "T_")) {
    MaybeAppend(state, "?");  // We don't support template substitutions.
    return true;
  }

  State copy = *state;
  if (ParseOneCharToken(state, 'T') && ParseNonNegativeNumber(state, nullptr) &&
      ParseOneCharToken(state, '_')) {
    MaybeAppend(state, "?");  // We don't support template substitutions.
    return true;
  }
  *state = copy;
  return false;
}

// <template-template-param> ::= <template-param>
//                           ::= <substitution>
bool ParseTemplateTemplateParam(State* state) {
  return (ParseTemplateParam(state) || ParseSubstitution(state));
}

// <template-args> ::= I <template-arg>+ E
bool ParseTemplateArgs(State* state) {
  State copy = *state;
  DisableAppend(state);
  if (ParseOneCharToken(state, 'I') && OneOrMore(ParseTemplateArg, state) &&
      Optional(ParseRequiresClause(state)) && ParseOneCharToken(state, 'E')) {
    RestoreAppend(state, copy.append);
    MaybeAppend(state, "<>");
    return true;
  }
  *state = copy;
  return false;
}

// <template-arg>  ::= <type>
//                 ::= <expr-primary>
//                 ::= I <template-arg>* E        # argument pack
//                 ::= J <template-arg>* E        # argument pack
//                 ::= X <expression> E
bool ParseTemplateArg(State* state) {
  ParseDepthGuard depth(state);
  if (!depth) {
    return false;
  }
  ParseTemplateArgGuard level(state);
  if (!level) {
    return false;
  }
  State original = *state;

  State copy = *state;
  if ((ParseOneCharToken(state, 'I') || ParseOneCharToken(state, 'J')) &&
      ZeroOrMore(ParseTemplateArg, state) && ParseOneCharToken(state, 'E')) {
    return true;
  }
  *state = copy;

  if (ParseType(state) || ParseExprPrimary(state)) {
    return true;
  }
  *state = copy;

  if (ParseOneCharToken(state, 'X') && ParseExpression(state) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  *state = copy;
  *state = original;
  return false;
}

// <function-param> ::= fp [<CV-qualifiers>] _
//                    ::= fp [<CV-qualifiers>] <number> _
//                    ::= fL <number> p [<CV-qualifiers>] _
//                    ::= fpT
bool ParseFunctionParam(State* state) {
  State copy = *state;
  if (ParseTwoCharToken(state, "fp")) {
    Optional(ParseCVQualifiers(state));
    if (ParseOneCharToken(state, 'T') ||
        (Optional(ParseNonNegativeNumber(state, nullptr)) &&
         ParseOneCharToken(state, '_'))) {
      return true;
    }
  }
  *state = copy;

  if (ParseTwoCharToken(state, "fL") &&
      ParseNonNegativeNumber(state, nullptr) && ParseOneCharToken(state, 'p')) {
    Optional(ParseCVQualifiers(state));
    if (Optional(ParseNonNegativeNumber(state, nullptr)) &&
        ParseOneCharToken(state, '_')) {
      return true;
    }
  }
  *state = copy;
  return false;
}

bool ParseSimpleId(State* state) {
  State copy = *state;
  if (ParseSourceName(state) && Optional(ParseTemplateArgs(state))) {
    return true;
  }
  *state = copy;
  return false;
}

bool ParseUnresolvedType(State* state) {
  State copy = *state;
  if (ParseTemplateParam(state) && Optional(ParseTemplateArgs(state))) {
    return true;
  }
  *state = copy;

  if (ParseSubstitution(state)) {
    return true;
  }
  *state = copy;

  if ((ParseTwoCharToken(state, "Dt") || ParseTwoCharToken(state, "DT")) &&
      ParseExpression(state) && ParseOneCharToken(state, 'E')) {
    return true;
  }
  *state = copy;
  return false;
}

bool ParseBaseUnresolvedName(State* state) {
  State copy = *state;
  if (ParseSimpleId(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "on") && ParseOperatorName(state) &&
      Optional(ParseTemplateArgs(state))) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "dn") &&
      (ParseUnresolvedType(state) || ParseSimpleId(state))) {
    return true;
  }
  *state = copy;
  return false;
}

// <unresolved-name> ::= [gs] <base-unresolved-name>
//                     ::= sr <unresolved-type> <base-unresolved-name>
//                     ::= sr N <unresolved-type>+ E <base-unresolved-name>
bool ParseUnresolvedName(State* state) {
  State copy = *state;
  if (ParseBaseUnresolvedName(state)) {
    return true;
  }
  *state = copy;

  if (Optional(ParseTwoCharToken(state, "gs")) &&
      ParseBaseUnresolvedName(state)) {
    return true;
  }
  *state = copy;

  if (Optional(ParseTwoCharToken(state, "gs")) &&
      ParseTwoCharToken(state, "sr") && OneOrMore(ParseSimpleId, state) &&
      ParseOneCharToken(state, 'E') && ParseBaseUnresolvedName(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "sr") && OneOrMore(ParseSimpleId, state) &&
      ParseOneCharToken(state, 'E') && ParseBaseUnresolvedName(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "sr") && ParseUnresolvedType(state) &&
      ParseBaseUnresolvedName(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "sr") && ParseOneCharToken(state, 'N') &&
      ParseUnresolvedType(state) && OneOrMore(ParseSimpleId, state) &&
      ParseOneCharToken(state, 'E') && ParseBaseUnresolvedName(state)) {
    return true;
  }
  *state = copy;
  return false;
}

// <braced-expression> ::= <expression>
//                        ::= di <source-name> <braced-expression>
//                        ::= dx <expression> <braced-expression>
//                        ::= dX <expression> <expression> <braced-expression>
bool ParseBracedExpression(State* state) {
  ParseDepthGuard depth(state);
  if (!depth) {
    return false;
  }
  State copy = *state;
  if (ParseExpression(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "di") && ParseSourceName(state) &&
      ParseBracedExpression(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "dx") && ParseExpression(state) &&
      ParseBracedExpression(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "dX") && ParseExpression(state) &&
      ParseExpression(state) && ParseBracedExpression(state)) {
    return true;
  }
  *state = copy;
  return false;
}

bool ParseExpressionSequence(State* state, const char terminator,
                             const bool require_one) {
  bool parsed = false;
  while (state->mangled_cur[0] != terminator) {
    if (!ParseExpression(state)) {
      return false;
    }
    parsed = true;
  }
  return parsed || !require_one;
}

bool ParseBracedExpressionSequence(State* state) {
  while (state->mangled_cur[0] != 'E') {
    if (!ParseBracedExpression(state)) {
      return false;
    }
  }
  return true;
}

// <expression> ::= rq <requirement>+ E
//                ::= rQ <bare-function-type> _ <requirement>+ E
bool ParseRequiresExpression(State* state) {
  State copy = *state;
  if (ParseTwoCharToken(state, "rQ")) {
    while (state->mangled_cur[0] != '_' && state->mangled_cur[0] != '\0') {
      if (!ParseType(state)) {
        *state = copy;
        return false;
      }
    }
    if (!ParseOneCharToken(state, '_')) {
      *state = copy;
      return false;
    }
  } else if (!ParseTwoCharToken(state, "rq")) {
    return false;
  }

  bool parsed_requirement = false;
  while (state->mangled_cur[0] != 'E' && state->mangled_cur[0] != '\0') {
    if (ParseOneCharToken(state, 'X') && ParseExpression(state)) {
      Optional(ParseOneCharToken(state, 'N'));
      if (ParseOneCharToken(state, 'R') && !ParseName(state)) {
        *state = copy;
        return false;
      }
      parsed_requirement = true;
      continue;
    }
    *state = copy;
    return false;
  }
  if (!parsed_requirement || !ParseOneCharToken(state, 'E')) {
    *state = copy;
    return false;
  }
  return true;
}

// <expression> ::= <template-param>
//              ::= <expr-primary>
//              ::= <unary operator-name> <expression>
//              ::= <binary operator-name> <expression> <expression>
//              ::= <trinary operator-name> <expression> <expression>
//                  <expression>
//              ::= st <type>
//              ::= sr <type> <unqualified-name> <template-args>
//              ::= sr <type> <unqualified-name>
bool ParseExpression(State* state) {
  ParseDepthGuard depth(state);
  if (!depth) {
    return false;
  }
  if (ParseTemplateParam(state) || ParseFunctionParam(state) ||
      ParseExprPrimary(state) || ParseRequiresExpression(state)) {
    return true;
  }

  ParseExpressionGuard level(state);
  if (!level) {
    return false;
  }
  State original = *state;

  State copy = *state;
  if ((ParseTwoCharToken(state, "pp") || ParseTwoCharToken(state, "mm")) &&
      ParseOneCharToken(state, '_') && ParseExpression(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "cv") && ParseType(state)) {
    if (ParseExpression(state)) {
      return true;
    }
    *state = copy;
    if (ParseTwoCharToken(state, "cv") && ParseType(state) &&
        ParseOneCharToken(state, '_') &&
        ParseExpressionSequence(state, 'E', false) &&
        ParseOneCharToken(state, 'E')) {
      return true;
    }
  }
  *state = copy;

  if (ParseTwoCharToken(state, "ti") && ParseType(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "te") && ParseExpression(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "at") && ParseType(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "az") && ParseExpression(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "nx") && ParseExpression(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "tw") && ParseExpression(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "tr")) {
    return true;
  }
  *state = copy;

  if (Optional(ParseOneCharToken(state, 'g')) &&
      (ParseTwoCharToken(state, "nw") || ParseTwoCharToken(state, "na"))) {
    while (state->mangled_cur[0] != '_' && state->mangled_cur[0] != '\0') {
      if (!ParseExpression(state)) {
        break;
      }
    }
    if (ParseOneCharToken(state, '_') && ParseType(state) &&
        (ParseOneCharToken(state, 'E') || ParseInitializer(state))) {
      return true;
    }
  }
  *state = copy;

  if (Optional(ParseOneCharToken(state, 'g')) &&
      (ParseTwoCharToken(state, "dl") || ParseTwoCharToken(state, "da")) &&
      ParseExpression(state)) {
    return true;
  }
  *state = copy;

  if ((ParseTwoCharToken(state, "fl") || ParseTwoCharToken(state, "fr")) &&
      ParseOperatorName(state) && ParseExpression(state)) {
    return true;
  }
  *state = copy;

  if ((ParseTwoCharToken(state, "fL") || ParseTwoCharToken(state, "fR")) &&
      ParseOperatorName(state) && ParseExpression(state) &&
      ParseExpression(state)) {
    return true;
  }
  *state = copy;

  if (ParseOneCharToken(state, 'u') && ParseSourceName(state) &&
      ZeroOrMore(ParseTemplateArg, state) && ParseOneCharToken(state, 'E')) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "cl") &&
      ParseExpressionSequence(state, 'E', true) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "cp") && ParseUnresolvedName(state) &&
      ParseExpressionSequence(state, 'E', false) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  *state = copy;

  if ((ParseTwoCharToken(state, "dc") || ParseTwoCharToken(state, "sc") ||
       ParseTwoCharToken(state, "cc") || ParseTwoCharToken(state, "rc")) &&
      ParseType(state) && ParseExpression(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "tl") && ParseType(state) &&
      ParseBracedExpressionSequence(state) && ParseOneCharToken(state, 'E')) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "il") && ParseBracedExpressionSequence(state) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "ds") && ParseExpression(state) &&
      ParseExpression(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "sZ") &&
      (ParseTemplateParam(state) || ParseFunctionParam(state))) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "sP") && ZeroOrMore(ParseTemplateArg, state) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "dt") && ParseExpression(state) &&
      ParseUnresolvedName(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "pt") && ParseExpression(state) &&
      ParseUnresolvedName(state)) {
    return true;
  }
  *state = copy;

  if (ParseUnresolvedName(state)) {
    return true;
  }
  *state = copy;

  if (ParseOperatorNameWithArity(state, OperatorArity::kTernary) &&
      ParseExpression(state) && ParseExpression(state) &&
      ParseExpression(state)) {
    return true;
  }
  *state = copy;

  if (ParseOperatorNameWithArity(state, OperatorArity::kBinary) &&
      ParseExpression(state) && ParseExpression(state)) {
    return true;
  }
  *state = copy;

  if (ParseOperatorNameWithArity(state, OperatorArity::kUnary) &&
      ParseExpression(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "st") && ParseType(state)) {
    return true;
  }
  *state = copy;

  State qualified_constraint = *state;
  if (ParseTwoCharToken(state, "sr") && ParseSourceName(state) &&
      ParseOneCharToken(state, 'E') && ParseUnqualifiedName(state) &&
      ParseTemplateArgs(state)) {
    return true;
  }
  *state = qualified_constraint;

  if (ParseTwoCharToken(state, "sr") && ParseType(state) &&
      ParseUnqualifiedName(state) && ParseTemplateArgs(state)) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "sr") && ParseType(state) &&
      ParseUnqualifiedName(state)) {
    return true;
  }
  *state = copy;

  // Pack expansion
  if (ParseTwoCharToken(state, "sp") && ParseExpression(state)) {
    return true;
  }
  *state = copy;

  *state = original;
  return false;
}

// <initializer> ::= pi <expression>* E
bool ParseInitializer(State* state) {
  return ParseTwoCharToken(state, "pi") &&
         ParseExpressionSequence(state, 'E', false) &&
         ParseOneCharToken(state, 'E');
}

// <expr-primary> ::= L <type> <(value) number> E
//                ::= L <type> <(value) float> E
//                ::= L <mangled-name> E
//                // A bug in g++'s C++ ABI version 2 (-fabi-version=2).
//                ::= LZ <encoding> E
bool ParseExprPrimary(State* state) {
  State copy = *state;
  if (ParseOneCharToken(state, 'L') && ParseType(state) &&
      ParseNumber(state, nullptr) && ParseOneCharToken(state, 'E')) {
    return true;
  }
  *state = copy;

  if (ParseOneCharToken(state, 'L') && ParseType(state) &&
      ParseFloatNumber(state) &&
      (ParseOneCharToken(state, 'E') ||
       (ParseOneCharToken(state, '_') && ParseFloatNumber(state) &&
        ParseOneCharToken(state, 'E')))) {
    return true;
  }
  *state = copy;

  if (ParseOneCharToken(state, 'L') && ParseTwoCharToken(state, "Dn") &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  *state = copy;

  if (ParseOneCharToken(state, 'L') && ParseMangledName(state) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  *state = copy;

  if (ParseTwoCharToken(state, "LZ") && ParseEncoding(state) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  *state = copy;

  // String literals are encoded by their array type without their value.
  // Keep the type out of the abbreviated output to preserve the parser's
  // small output footprint.
  if (ParseOneCharToken(state, 'L')) {
    const bool previous_append = state->append;
    DisableAppend(state);
    if (ParseArrayType(state) && ParseOneCharToken(state, 'E')) {
      RestoreAppend(state, previous_append);
      return true;
    }
  }
  *state = copy;

  return false;
}

// <local-name> := Z <(function) encoding> E <(entity) name>
//                 [<discriminator>]
//              := Z <(function) encoding> E s [<discriminator>]
bool ParseLocalName(State* state) {
  ParseDepthGuard depth(state);
  if (!depth) {
    return false;
  }
  ParseLocalNameGuard level(state);
  if (!level) {
    return false;
  }
  State original = *state;

  State copy = *state;
  if (ParseOneCharToken(state, 'Z') && ParseEncoding(state) &&
      ParseOneCharToken(state, 'E') && MaybeAppend(state, "::") &&
      ParseName(state) && Optional(ParseDiscriminator(state))) {
    return true;
  }
  *state = copy;

  if (ParseOneCharToken(state, 'Z') && ParseEncoding(state) &&
      ParseTwoCharToken(state, "Es") && Optional(ParseDiscriminator(state))) {
    return true;
  }
  *state = copy;

  if (ParseOneCharToken(state, 'Z') && ParseEncoding(state) &&
      ParseTwoCharToken(state, "Ed") &&
      ParseNonNegativeNumber(state, nullptr) && ParseOneCharToken(state, '_') &&
      MaybeAppend(state, "::") && ParseName(state) &&
      Optional(ParseDiscriminator(state))) {
    return true;
  }
  *state = original;
  return false;
}

// <discriminator> := _ <(non-negative) number>
bool ParseDiscriminator(State* state) {
  State copy = *state;
  std::int64_t number = 0;
  if (ParseTwoCharToken(state, "__") &&
      ParseNonNegativeNumber(state, &number) && number >= 10 &&
      ParseOneCharToken(state, '_')) {
    return true;
  }
  *state = copy;

  if (ParseOneCharToken(state, '_') && ParseNonNegativeNumber(state, &number) &&
      number < 10) {
    return true;
  }
  *state = copy;
  return false;
}

// <substitution> ::= S_
//                ::= S <seq-id> _
//                ::= St, etc.
bool ParseSubstitution(State* state) {
  if (ParseTwoCharToken(state, "S_")) {
    Optional(ParseAbiTags(state));
    MaybeAppend(state, "?");  // We don't support substitutions.
    return true;
  }

  State copy = *state;
  if (ParseOneCharToken(state, 'S') && ParseSeqId(state) &&
      ParseOneCharToken(state, '_')) {
    Optional(ParseAbiTags(state));
    MaybeAppend(state, "?");  // We don't support substitutions.
    return true;
  }
  *state = copy;

  // Expand abbreviations like "St" => "std".
  if (ParseOneCharToken(state, 'S')) {
    const AbbrevPair* p;
    for (p = kSubstitutionList; p->abbrev != nullptr; ++p) {
      if (state->mangled_cur[0] == p->abbrev[1]) {
        MaybeAppend(state, "std");
        if (p->real_name[0] != '\0') {
          MaybeAppend(state, "::");
          MaybeAppend(state, p->real_name);
        }
        ++state->mangled_cur;
        Optional(ParseAbiTags(state));
        return true;
      }
    }
  }
  *state = copy;
  return false;
}

// Parse <mangled-name>, optionally followed by either a function-clone suffix
// or version suffix.  Returns true only if all of "mangled_cur" was consumed.
bool ParseTopLevelMangledName(State* state) {
  if (ParseMangledName(state)) {
    if (state->mangled_cur[0] != '\0') {
      // Drop trailing function clone suffix, if any.
      if (IsFunctionCloneSuffix(state->mangled_cur)) {
        return true;
      }
      // Append trailing version suffix if any.
      // ex. _Z3foo@@GLIBCXX_3.4
      if (state->mangled_cur[0] == '@') {
        MaybeAppend(state, state->mangled_cur);
        return true;
      }
      if (state->mangled_cur[0] == '.' && state->mangled_cur[1] != '\0') {
        return true;
      }
      return false;
    }
    return true;
  }
  return false;
}
}  // namespace
#endif

bool ValidDemangleArguments(const char* mangled, const char* out,
                            std::size_t out_size) {
  return mangled != nullptr && out != nullptr && out_size != 0;
}

// The system demangler is kept separate because it may allocate memory.
bool DemangleWithSystem(const char* mangled, char* out, std::size_t out_size) {
  if (out != nullptr && out_size != 0) {
    out[0] = '\0';
  }
  if (!ValidDemangleArguments(mangled, out, out_size)) {
    return false;
  }

#if defined(HAVE___CXA_DEMANGLE) && \
    !(defined(NGLOG_OS_WINDOWS) && defined(_MSC_VER))
  int status = -1;
  std::size_t n = 0;
  std::unique_ptr<char, decltype(&std::free)> unmangled{
      abi::__cxa_demangle(mangled, nullptr, &n, &status), &std::free};

  if (!unmangled || status != 0) {
    return false;
  }

  // n is the size of the buffer __cxa_demangle() allocated, not the length of
  // the demangled name. A name that does not fit is a failure, not a success
  // with the name silently cut off.
  const auto* end =
      static_cast<const char*>(std::memchr(unmangled.get(), '\0', n));

  if (end == nullptr) {
    return false;
  }

  const std::ptrdiff_t length = end - unmangled.get() + 1;

  if (static_cast<std::size_t>(length) > out_size) {
    return false;
  }

  std::copy_n(unmangled.get(), length, out);
  return true;
#elif defined(NGLOG_OS_WINDOWS)
#  if defined(HAVE_DBGHELP)
  // When built with incremental linking, the Windows debugger
  // library provides a more complicated `Symbol->Name` with the
  // Incremental Linking Table offset, which looks like
  // `@ILT+1105(?func@Foo@@SAXH@Z)`. However, the demangler expects
  // only the mangled symbol, `?func@Foo@@SAXH@Z`. Fortunately, the
  // mangled symbol is guaranteed not to have parentheses,
  // so we search for `(` and extract up to `)`.
  //
  // Avoid `std::string` because this API is also used by low-level callers.
  constexpr std::size_t kWindowsSymbolBufferSize = 1024;
  char buffer[kWindowsSymbolBufferSize];
  const char* lparen = std::strchr(mangled, '(');
  if (lparen) {
    // Extract the string `(?...)`
    const char* rparen = std::strchr(lparen, ')');
    if (rparen == nullptr) {
      return false;
    }
    const std::size_t length = static_cast<std::size_t>(rparen - lparen) - 1;
    if (length >= sizeof(buffer)) {
      return false;
    }
    std::memcpy(buffer, lparen + 1, length);
    buffer[length] = '\0';
    mangled = buffer;
  }  // Else the symbol wasn't inside a set of parentheses
  // We use the ANSI version to ensure the string type is always `char *`.
  return UnDecorateSymbolName(mangled, out, out_size, UNDNAME_COMPLETE);
#  else
  (void)mangled;
  (void)out;
  (void)out_size;
  return false;
#  endif
#else
  (void)mangled;
  (void)out;
  (void)out_size;
  return false;
#endif
}

// The local parser is the default because it does not allocate memory or
// invoke a platform demangler. This keeps the Itanium path suitable for use
// from failure signal handlers.
bool Demangle(const char* mangled, char* out, std::size_t out_size) {
  if (out != nullptr && out_size != 0) {
    out[0] = '\0';
  }
  if (!ValidDemangleArguments(mangled, out, out_size)) {
    return false;
  }

#if defined(_MSC_VER)
  return false;
#else
  State state;
  InitState(&state, mangled, out, out_size);
  return ParseTopLevelMangledName(&state) && !state.overflowed;
#endif
}

}  // namespace tools
}  // namespace nglog
