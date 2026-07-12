# SPDX-License-Identifier: BSD-3-Clause
#
# - Try to find libbacktrace
# Once done this will define
#
#  Libbacktrace_FOUND - system has libbacktrace
#  libbacktrace::libbacktrace - cmake target for libbacktrace

include (FindPackageHandleStandardArgs)

find_path (Libbacktrace_INCLUDE_DIR NAMES backtrace.h DOC "libbacktrace include directory")
find_library (Libbacktrace_LIBRARY NAMES backtrace DOC "libbacktrace library")

mark_as_advanced (Libbacktrace_INCLUDE_DIR Libbacktrace_LIBRARY)

find_package_handle_standard_args (Libbacktrace
  REQUIRED_VARS Libbacktrace_INCLUDE_DIR Libbacktrace_LIBRARY
)

if (Libbacktrace_FOUND)
  if (NOT TARGET libbacktrace::libbacktrace)
    add_library (libbacktrace::libbacktrace INTERFACE IMPORTED)

    set_property (TARGET libbacktrace::libbacktrace PROPERTY
      INTERFACE_INCLUDE_DIRECTORIES ${Libbacktrace_INCLUDE_DIR}
    )
    set_property (TARGET libbacktrace::libbacktrace PROPERTY
      INTERFACE_LINK_LIBRARIES ${Libbacktrace_LIBRARY}
    )
  endif (NOT TARGET libbacktrace::libbacktrace)
endif (Libbacktrace_FOUND)
