# SPDX-FileCopyrightText: 2026 The ng-log contributors
#
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required (VERSION 3.28)
project (ng-log_attributes LANGUAGES CXX)

find_package (ng-log REQUIRED NO_MODULE)

add_executable (ng-log_attribute_format ng-log_attribute_format.cc)
target_link_libraries (ng-log_attribute_format PRIVATE ng-log::ng-log)
target_compile_options (ng-log_attribute_format PRIVATE -Werror=format)
