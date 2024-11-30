cmake_minimum_required (VERSION 3.16)
project (ng-log_includes LANGUAGES CXX)

find_package (ng-log REQUIRED NO_MODULE)

add_executable (ng-log_includes_logging ng-log_includes_logging.cc)
target_link_libraries (ng-log_includes_logging PRIVATE ng-log::ng-log)

add_executable (ng-log_includes_vlog_is_on ng-log_includes_vlog_is_on.cc)
target_link_libraries (ng-log_includes_vlog_is_on PRIVATE ng-log::ng-log)

add_executable (ng-log_includes_raw_logging ng-log_includes_raw_logging.cc)
target_link_libraries (ng-log_includes_raw_logging PRIVATE ng-log::ng-log)

add_executable (ng-log_includes_stl_logging ng-log_includes_stl_logging.cc)
target_link_libraries (ng-log_includes_stl_logging PRIVATE ng-log::ng-log)
