# SPDX-FileCopyrightText: 2026 The ng-log contributors
#
# SPDX-License-Identifier: BSD-3-Clause

# Checks whether the C++ compiler supports the experimental logging functions,
# which require C++20 while the remainder of ng-log keeps building with C++14.
# Both the build of ng-log and its package configuration run the check, the
# latter with the compiler of the consumer. The check shares the requirements
# of the header found in the INCLUDES directories and runs in a function, which
# keeps the language standard it selects local. QUIET suppresses its status
# messages.

include (CheckCXXSourceCompiles)
include (CMakePushCheckState)

function (nglog_check_experimental_logging _RESULT)
  cmake_parse_arguments (PARSE_ARGV 1 _ARGS "QUIET" "" "INCLUDES")

  if (DEFINED ${_RESULT})
    return ()
  endif (DEFINED ${_RESULT})

  if (NOT cxx_std_20 IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    if (NOT _ARGS_QUIET)
      message (STATUS "Performing Test ${_RESULT} - Failed (no C++20 mode)")
    endif (NOT _ARGS_QUIET)
    set (${_RESULT} "" CACHE INTERNAL "Test ${_RESULT}")
    return ()
  endif (NOT cxx_std_20 IN_LIST CMAKE_CXX_COMPILE_FEATURES)

  # Any newer standard is fine. C++98 orders after C++20 numerically.
  if (NOT DEFINED CMAKE_CXX_STANDARD OR CMAKE_CXX_STANDARD EQUAL 98 OR
      CMAKE_CXX_STANDARD LESS 20)
    set (CMAKE_CXX_STANDARD 20)
  endif (NOT DEFINED CMAKE_CXX_STANDARD OR CMAKE_CXX_STANDARD EQUAL 98 OR
      CMAKE_CXX_STANDARD LESS 20)

  cmake_push_check_state (RESET)
  set (CMAKE_REQUIRED_INCLUDES ${_ARGS_INCLUDES})
  set (CMAKE_REQUIRED_QUIET ${_ARGS_QUIET})
  check_cxx_source_compiles ("
#define NGLOG_INTERNAL_EXPERIMENTAL_LOGGING_CHECK
#include <ng-log/experimental/requirements.h>

#include <format>
#include <source_location>
#include <string>

int main()
{
  const std::string text =
      std::format(\"{}\", std::source_location::current().line());
  return text.empty() ? 1 : 0;
}
" ${_RESULT})
  cmake_pop_check_state ()
endfunction (nglog_check_experimental_logging)
