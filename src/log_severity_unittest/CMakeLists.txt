cmake_minimum_required (VERSION 3.16)
project (ng-log_log_severity LANGUAGES CXX)

find_package (ng-log REQUIRED NO_MODULE)

add_executable (ng-log_log_severity_constants ng-log_log_severity_constants.cc)
target_link_libraries (ng-log_log_severity_constants PRIVATE ng-log::ng-log)

add_executable (ng-log_log_severity_conversion ng-log_log_severity_conversion.cc)
target_link_libraries (ng-log_log_severity_conversion PRIVATE ng-log::ng-log)
