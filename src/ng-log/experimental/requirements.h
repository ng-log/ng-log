// SPDX-FileCopyrightText: 2026 The ng-log contributors
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Author: Sergiu Deitsch

// Language and library requirements of the experimental logging functions.
// Defines NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_SUPPORTED if all are met and
// fails with a diagnostic naming the first unmet requirement otherwise. The
// build checks the requirements using this header as well.

#ifndef NGLOG_EXPERIMENTAL_REQUIREMENTS_H
#define NGLOG_EXPERIMENTAL_REQUIREMENTS_H

// The library records in the export header whether it lacks the functions,
// which only the headers of a build tree can encounter. The checks of the
// requirements ignore the header, which may belong to another installation.
#if !defined(NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_CHECK) && \
    __has_include(<ng-log/export.h>)
#  include <ng-log/export.h>  // IWYU pragma: keep
#endif

// MSVC reports the language level in _MSVC_LANG unless /Zc:__cplusplus is used.
// <version> is checked for only afterwards since it is part of C++20 only.
#if defined(NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_UNAVAILABLE)
#  error \
      "ng-log was built without the experimental logging functions (WITH_EXPERIMENTAL=OFF or an unsupported compiler)"
#elif (defined(_MSVC_LANG) && _MSVC_LANG < 202002L) || \
    (!defined(_MSVC_LANG) && __cplusplus < 202002L)
#  error "ng-log experimental logging requires C++20 (__cplusplus >= 202002L)"
#elif !__has_include(<version>)
#  error "ng-log experimental logging requires C++20 <version> header"
#else
#  include <version>
#  if !defined(__cpp_concepts) || __cpp_concepts < 201907L
#    error \
        "ng-log experimental logging requires concepts (__cpp_concepts >= 201907L)"
#  elif !defined(__cpp_consteval) || __cpp_consteval < 201811L
#    error \
        "ng-log experimental logging requires consteval (__cpp_consteval >= 201811L)"
#  elif !defined(__cpp_generic_lambdas) || __cpp_generic_lambdas < 201707L
#    error \
        "ng-log experimental logging requires template lambdas (__cpp_generic_lambdas >= 201707L)"
#  elif !defined(__cpp_lib_concepts) || __cpp_lib_concepts < 202002L
#    error \
        "ng-log experimental logging requires <concepts> (__cpp_lib_concepts >= 202002L)"
#  elif !defined(__cpp_lib_type_identity) || __cpp_lib_type_identity < 201806L
#    error \
        "ng-log experimental logging requires std::type_identity (__cpp_lib_type_identity >= 201806L)"
#  elif !defined(__cpp_lib_format) || __cpp_lib_format < 202106L
#    error \
        "ng-log experimental logging requires std::format with compile-time checked format strings (__cpp_lib_format >= 202106L)"
#  elif !defined(__cpp_lib_constexpr_string) || \
      __cpp_lib_constexpr_string < 201907L
#    error \
        "ng-log experimental logging requires constexpr std::string (__cpp_lib_constexpr_string >= 201907L)"
#  elif !defined(__cpp_lib_source_location) || \
      __cpp_lib_source_location < 201907L
#    error \
        "ng-log experimental logging requires std::source_location (__cpp_lib_source_location >= 201907L)"
#  else
// Compiling the declarations with unmet requirements would only bury the
// diagnostic above under unrelated errors.
#    define NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_SUPPORTED
#  endif
#endif

#endif  // NGLOG_EXPERIMENTAL_REQUIREMENTS_H
