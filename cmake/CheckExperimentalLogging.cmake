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

# Checks whether the generator and the C++ compiler can import the module of
# the experimental logging functions from an imported target, which requires
# the experimental logging functions to be supported. Only Ninja generators
# import the modules of imported targets, and only toolchains that scan sources
# for module dependencies and compile the module interfaces of imported
# targets, as cmake-cxxmodules(7) describes. CMake reports the lack of either
# as an error, even within try_compile, so the check inspects the toolchain
# instead of compiling. QUIET suppresses the status message.
function (nglog_check_experimental_module _RESULT)
  cmake_parse_arguments (PARSE_ARGV 1 _ARGS "QUIET" "" "")

  set (_REASON)

  if (NOT CMAKE_GENERATOR MATCHES "^Ninja")
    set (_REASON "the ${CMAKE_GENERATOR} generator cannot import modules")
  elseif (CMAKE_VERSION VERSION_LESS 3.28.4)
    # Imported modules are compiled without the include directories of their
    # dependencies.
    set (_REASON "CMake ${CMAKE_VERSION} compiles imported modules without the \
usage requirements of their dependencies, which requires CMake 3.28.4 or newer")
  elseif (NOT DEFINED CMAKE_CXX_SCANDEP_SOURCE OR
      (NOT DEFINED CMAKE_CXX_COMPILE_BMI AND
       NOT DEFINED CMAKE_CXX_MODULE_BMI_ONLY_FLAG))
    set (_REASON "the C++ compiler ${CMAKE_CXX_COMPILER_ID} \
${CMAKE_CXX_COMPILER_VERSION} cannot import modules")
  elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
      CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)
    # Importers fail to deduce the template arguments of Log.
    set (_REASON "GCC ${CMAKE_CXX_COMPILER_VERSION} cannot deduce the template \
arguments of Log through the module, which requires GCC 15 or newer")
  endif (NOT CMAKE_GENERATOR MATCHES "^Ninja")

  if (_REASON)
    set (${_RESULT} OFF PARENT_SCOPE)
    set (_STATUS "Failed (${_REASON})")
  else (_REASON)
    set (${_RESULT} ON PARENT_SCOPE)
    set (_STATUS "Success")
  endif (_REASON)

  if (NOT _ARGS_QUIET)
    message (STATUS "Performing Test ${_RESULT} - ${_STATUS}")
  endif (NOT _ARGS_QUIET)
endfunction (nglog_check_experimental_module)
