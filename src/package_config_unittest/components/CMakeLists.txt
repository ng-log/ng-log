# SPDX-FileCopyrightText: 2026 The ng-log contributors
#
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required (VERSION 3.28)
project (ng-log_package_components LANGUAGES CXX)

set (NGLOG_REQUIRED_COMPONENTS "" CACHE STRING
  "Components of ng-log to require")
set (NGLOG_OPTIONAL_COMPONENTS "" CACHE STRING
  "Components of ng-log to request optionally")
option (NGLOG_EXPECT_EXPERIMENTAL
  "Whether the experimental component is expected to be found" OFF)

find_package (ng-log REQUIRED NO_MODULE
  COMPONENTS ${NGLOG_REQUIRED_COMPONENTS}
  OPTIONAL_COMPONENTS ${NGLOG_OPTIONAL_COMPONENTS})

if (NOT "${ng-log_experimental_FOUND}" STREQUAL "${NGLOG_EXPECT_EXPERIMENTAL}")
  message (FATAL_ERROR "Expected ng-log_experimental_FOUND to be "
    "${NGLOG_EXPECT_EXPERIMENTAL} but got '${ng-log_experimental_FOUND}'")
endif (NOT "${ng-log_experimental_FOUND}" STREQUAL "${NGLOG_EXPECT_EXPERIMENTAL}")

if (ng-log_experimental_FOUND)
  # The target requires C++20 by itself.
  add_executable (ng-log_package_components ng-log_package_experimental.cc)
  target_link_libraries (ng-log_package_components PRIVATE
    ng-log::experimental)
else (ng-log_experimental_FOUND)
  if (TARGET ng-log::experimental)
    message (FATAL_ERROR "ng-log::experimental exists although the "
      "experimental component was not found")
  endif (TARGET ng-log::experimental)

  add_executable (ng-log_package_components
    ../working_config/ng-log_package_config.cc)
  target_link_libraries (ng-log_package_components PRIVATE ng-log::ng-log)
endif (ng-log_experimental_FOUND)
