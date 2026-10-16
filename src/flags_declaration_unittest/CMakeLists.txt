# SPDX-FileCopyrightText: 2026 The ng-log contributors
#
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required (VERSION 3.16)
project (ng-log_flags_declaration LANGUAGES CXX)

find_package (ng-log REQUIRED NO_MODULE)

add_executable (ng-log_flags_builtin_redeclaration
  ng-log_flags_builtin_redeclaration.cc)
target_link_libraries (ng-log_flags_builtin_redeclaration PRIVATE
  ng-log::ng-log)

add_executable (ng-log_flags_declaration ng-log_flags_declaration.cc)
# Standard C++ without compiler extensions. The ng-log headers are not treated
# as system headers, so that warnings in their macros are reported.
set_target_properties (ng-log_flags_declaration PROPERTIES
  CXX_STANDARD 14
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  NO_SYSTEM_FROM_IMPORTED ON)
target_link_libraries (ng-log_flags_declaration PRIVATE ng-log::ng-log)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options (ng-log_flags_declaration PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
