cmake_minimum_required (VERSION 3.16)
project (ng-log_log_severity LANGUAGES CXX)

find_package (ng-log REQUIRED NO_MODULE)

add_executable (ng-log_dcheck ng-log_dcheck.cc)
target_link_libraries (ng-log_dcheck PRIVATE ng-log::ng-log)
