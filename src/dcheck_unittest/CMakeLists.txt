# SPDX-FileCopyrightText: 2024 Google Inc.
# SPDX-FileCopyrightText: 2026 The ng-log contributors
#
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required (VERSION 3.28)
project (ng-log_log_severity LANGUAGES CXX)

find_package (ng-log REQUIRED NO_MODULE)

add_executable (ng-log_dcheck ng-log_dcheck.cc)
target_link_libraries (ng-log_dcheck PRIVATE ng-log::ng-log)

# Windows looks up the ng-log DLL next to the executable, which also works under
# Wine where the search path of the build machine does not apply.
if (WIN32)
  add_custom_command (TARGET ng-log_dcheck POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:ng-log_dcheck>
      $<TARGET_RUNTIME_DLLS:ng-log_dcheck>
    COMMAND_EXPAND_LISTS)
endif (WIN32)
