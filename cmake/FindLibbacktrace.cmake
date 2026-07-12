# SPDX-FileCopyrightText: 2026 The ng-log contributors
# SPDX-License-Identifier: BSD-3-Clause
#
#[=======================================================================[.rst:
FindLibbacktrace
----------------

Finds the libbacktrace library.

.. code-block:: cmake

  find_package (Libbacktrace)

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported target when libbacktrace is found:

``libbacktrace::libbacktrace``
  Target that provides the include directory and library for libbacktrace.

Result Variables
^^^^^^^^^^^^^^^^

This module defines the following variable:

``Libbacktrace_FOUND``
  Boolean indicating whether libbacktrace was found.

Cache Variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``Libbacktrace_INCLUDE_DIR``
  The directory containing ``backtrace.h``.

``Libbacktrace_LIBRARY``
  The libbacktrace library.
#]=======================================================================]

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
