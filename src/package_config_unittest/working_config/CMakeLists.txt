cmake_minimum_required (VERSION 3.16)
project (ng-log_package_config LANGUAGES CXX)

find_package (ng-log REQUIRED NO_MODULE)

add_executable (ng-log_package_config ng-log_package_config.cc)

target_link_libraries (ng-log_package_config PRIVATE ng-log::ng-log)
