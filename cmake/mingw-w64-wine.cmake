# Cross-compiling toolchain for x86_64-w64-mingw32, with test binaries run
# through Wine so that "cmake --build" followed by "ctest" behaves the same
# as a native build.
#
# Usage:
#   cmake -S . -B build-mingw -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-wine.cmake

set (CMAKE_SYSTEM_NAME Windows)
set (CMAKE_SYSTEM_PROCESSOR AMD64)

# FIXME Do we actually need the prefix? The executables are expected to be in
# the PATH
set (_ng-log_MINGW_PREFIX x86_64-w64-mingw32)

find_program (CMAKE_C_COMPILER ${_ng-log_MINGW_PREFIX}-gcc REQUIRED)
find_program (CMAKE_CXX_COMPILER ${_ng-log_MINGW_PREFIX}-g++ REQUIRED)
find_program (CMAKE_RC_COMPILER ${_ng-log_MINGW_PREFIX}-windres)

set (CMAKE_FIND_ROOT_PATH /usr/${_ng-log_MINGW_PREFIX})
set (CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set (CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set (CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set (CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

find_program (CMAKE_CROSSCOMPILING_EMULATOR wine REQUIRED)

unset (_ng-log_MINGW_PREFIX)
