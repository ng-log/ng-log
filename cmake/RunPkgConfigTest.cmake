# SPDX-FileCopyrightText: 2026 The ng-log contributors
#
# SPDX-License-Identifier: BSD-3-Clause

# Installs the build, moves the installation elsewhere and expects pkg-config
# to report the paths of the new location and a library that exists there.

cmake_minimum_required (VERSION 3.28...4.4)

file (REMOVE_RECURSE "${TEST_DIR}")
set (_INSTALLED_PREFIX "${TEST_DIR}/installed")
set (_MOVED_PREFIX "${TEST_DIR}/moved")

execute_process (
  COMMAND ${CMAKE_COMMAND} --install "${BINARY_DIR}" --config "${CONFIG}"
    --prefix "${_INSTALLED_PREFIX}"
  OUTPUT_QUIET
  RESULT_VARIABLE _RESULT)

if (NOT _RESULT EQUAL 0)
  message (FATAL_ERROR "Failed to install ${BINARY_DIR} (error: ${_RESULT})")
endif (NOT _RESULT EQUAL 0)

file (RENAME "${_INSTALLED_PREFIX}" "${_MOVED_PREFIX}")
set (ENV{PKG_CONFIG_LIBDIR} "${_MOVED_PREFIX}/${LIBDIR}/pkgconfig")
unset (ENV{PKG_CONFIG_PATH})

foreach (_QUERY IN ITEMS cflags libs)
  execute_process (
    COMMAND "${PKG_CONFIG}" --${_QUERY} libng-log
    OUTPUT_VARIABLE _OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _RESULT)

  if (NOT _RESULT EQUAL 0)
    message (FATAL_ERROR "pkg-config --${_QUERY} failed (error: ${_RESULT})")
  endif (NOT _RESULT EQUAL 0)

  if (_QUERY STREQUAL cflags)
    set (_FLAG -I)
    file (REAL_PATH "${_MOVED_PREFIX}/${INCLUDEDIR}" _EXPECTED)
  else (_QUERY STREQUAL cflags)
    set (_FLAG -L)
    file (REAL_PATH "${_MOVED_PREFIX}/${LIBDIR}" _EXPECTED)
  endif (_QUERY STREQUAL cflags)

  # The reported paths may contain components such as .. that a comparison
  # must resolve first.
  set (_PATHS)
  separate_arguments (_ARGUMENTS NATIVE_COMMAND "${_OUTPUT}")

  foreach (_ARGUMENT IN LISTS _ARGUMENTS)
    if (_ARGUMENT MATCHES "^${_FLAG}(.+)$")
      file (REAL_PATH "${CMAKE_MATCH_1}" _PATH)
      list (APPEND _PATHS "${_PATH}")
    endif (_ARGUMENT MATCHES "^${_FLAG}(.+)$")
  endforeach (_ARGUMENT)

  if (NOT _EXPECTED IN_LIST _PATHS)
    message (SEND_ERROR
      "Expected pkg-config --${_QUERY} to contain ${_FLAG}${_EXPECTED} but got ${_OUTPUT}")
  endif (NOT _EXPECTED IN_LIST _PATHS)

  if (_QUERY STREQUAL libs)
    # The library name depends on the configuration, so the named library must
    # exist in the installation.
    set (_LIBRARIES)
    foreach (_ARGUMENT IN LISTS _ARGUMENTS)
      if (_ARGUMENT MATCHES "^-l(.+)$")
        file (GLOB _FILES "${_EXPECTED}/lib${CMAKE_MATCH_1}.*"
          "${_EXPECTED}/${CMAKE_MATCH_1}.lib")
        list (APPEND _LIBRARIES ${_FILES})
      endif (_ARGUMENT MATCHES "^-l(.+)$")
    endforeach (_ARGUMENT)

    if (NOT _LIBRARIES)
      message (SEND_ERROR
        "Expected pkg-config --libs to name a library in ${_EXPECTED} but got ${_OUTPUT}")
    endif (NOT _LIBRARIES)
  endif (_QUERY STREQUAL libs)
endforeach (_QUERY)
