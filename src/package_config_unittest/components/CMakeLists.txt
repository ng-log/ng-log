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
option (NGLOG_EXPECT_EXPERIMENTAL_MODULE
  "Whether ng-log::experimental is expected to provide the C++ module" OFF)
option (NGLOG_EXPECT_INSTALLED
  "Whether ng-log is expected to be found in an installation" OFF)

find_package (ng-log REQUIRED NO_MODULE
  COMPONENTS ${NGLOG_REQUIRED_COMPONENTS}
  OPTIONAL_COMPONENTS ${NGLOG_OPTIONAL_COMPONENTS})

# Unlike the build tree, an installation contains no CMake cache.
if (NGLOG_EXPECT_INSTALLED AND EXISTS ${ng-log_DIR}/CMakeCache.txt)
  message (FATAL_ERROR "Expected ng-log to be installed but found the build "
    "tree ${ng-log_DIR}")
endif (NGLOG_EXPECT_INSTALLED AND EXISTS ${ng-log_DIR}/CMakeCache.txt)

if (NOT "${ng-log_experimental_FOUND}" STREQUAL "${NGLOG_EXPECT_EXPERIMENTAL}")
  message (FATAL_ERROR "Expected ng-log_experimental_FOUND to be "
    "${NGLOG_EXPECT_EXPERIMENTAL} but got '${ng-log_experimental_FOUND}'")
endif (NOT "${ng-log_experimental_FOUND}" STREQUAL "${NGLOG_EXPECT_EXPERIMENTAL}")

if (ng-log_experimental_FOUND)
  # Without the module, the target is an interface library and the source
  # including the headers must still build.
  if (NGLOG_EXPECT_EXPERIMENTAL_MODULE)
    set (_SOURCE ng-log_package_experimental_module.cc)
  else (NGLOG_EXPECT_EXPERIMENTAL_MODULE)
    get_target_property (_TYPE ng-log::experimental TYPE)

    if (NOT _TYPE STREQUAL INTERFACE_LIBRARY)
      message (FATAL_ERROR "Expected ng-log::experimental without the module "
        "to be an INTERFACE_LIBRARY but got ${_TYPE}")
    endif (NOT _TYPE STREQUAL INTERFACE_LIBRARY)

    set (_SOURCE ng-log_package_experimental.cc)
  endif (NGLOG_EXPECT_EXPERIMENTAL_MODULE)

  # The target requires C++20 by itself.
  add_executable (ng-log_package_components ${_SOURCE})
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
