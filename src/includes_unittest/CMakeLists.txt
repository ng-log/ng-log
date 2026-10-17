# SPDX-FileCopyrightText: 2024 Google Inc.
# SPDX-FileCopyrightText: 2026 The ng-log contributors
#
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required (VERSION 3.28)
project (ng-log_includes LANGUAGES CXX)

find_package (ng-log REQUIRED NO_MODULE)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # Public headers must compile cleanly in projects that treat warnings as
  # errors. Headers of imported targets are system headers by default, which
  # would hide their warnings.
  add_compile_options (-Wall -Wextra -Wshadow -Werror)
  set (CMAKE_NO_SYSTEM_FROM_IMPORTED ON)
endif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")

add_executable (ng-log_includes_logging ng-log_includes_logging.cc)
target_link_libraries (ng-log_includes_logging PRIVATE ng-log::ng-log)

add_executable (ng-log_includes_log_severity ng-log_includes_log_severity.cc)
target_link_libraries (ng-log_includes_log_severity PRIVATE ng-log::ng-log)

add_executable (ng-log_includes_vlog_is_on ng-log_includes_vlog_is_on.cc)
target_link_libraries (ng-log_includes_vlog_is_on PRIVATE ng-log::ng-log)

add_executable (ng-log_includes_raw_logging ng-log_includes_raw_logging.cc)
target_link_libraries (ng-log_includes_raw_logging PRIVATE ng-log::ng-log)

add_executable (ng-log_includes_stl_logging ng-log_includes_stl_logging.cc)
target_link_libraries (ng-log_includes_stl_logging PRIVATE ng-log::ng-log)
