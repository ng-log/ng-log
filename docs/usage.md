# Using ng-log in a CMake Project

Assuming that ng-log was previously [built using CMake](build.md#cmake) or
installed using a package manager, you can use the CMake command `#!cmake
find_package` to build against ng-log in your CMake project as follows:

``` cmake title="CMakeLists.txt"
cmake_minimum_required (VERSION 3.16)
project (myproj VERSION 1.0)

find_package (ng-log 0.8.1 REQUIRED)

add_executable (myapp main.cpp)
target_link_libraries (myapp ng-log::ng-log)
```

Compile definitions and options will be added automatically to your target as
needed.

Alternatively, ng-log can be incorporated into using the CMake command `#!cmake
add_subdirectory` to include ng-log directly from a subdirectory of your project
by replacing the `#!cmake find_package` call from the previous snippet by
`add_subdirectory`. The `#!cmake ng-log::ng-log` target is in this case an
`#!cmake ALIAS` library target for the `ng-log` library target.
