# SPDX-FileCopyrightText: 2026 The ng-log contributors
#
# SPDX-License-Identifier: BSD-3-Clause

# Runs the command that follows "--" and requires it to fail with output that
# matches EXPECTED. CTest ignores the exit status of a test whose output it
# matches against an expression, so a build that is expected to fail would
# pass if it succeeded with a matching warning.
#
# Usage: cmake -DEXPECTED=<regex> -P RunFailingBuildTest.cmake -- <command>...

if (NOT DEFINED EXPECTED)
  message (FATAL_ERROR "RunFailingBuildTest.cmake requires EXPECTED")
endif (NOT DEFINED EXPECTED)

# Quotes each argument, since a list would drop empty ones, such as the
# configuration of a single-configuration generator without a build type.
set (_command)
set (_arguments 0)
set (_after_separator FALSE)

math (EXPR _last "${CMAKE_ARGC} - 1")

foreach (_index RANGE ${_last})
  if (_after_separator)
    set (_argument "${CMAKE_ARGV${_index}}")
    string (REPLACE "\\" "\\\\" _argument "${_argument}")
    string (REPLACE "\"" "\\\"" _argument "${_argument}")
    string (REPLACE "$" "\\$" _argument "${_argument}")
    string (REPLACE ";" "\\;" _argument "${_argument}")
    string (APPEND _command " \"${_argument}\"")
    math (EXPR _arguments "${_arguments} + 1")
  elseif (CMAKE_ARGV${_index} STREQUAL "--")
    set (_after_separator TRUE)
  endif (_after_separator)
endforeach (_index)

if (_arguments EQUAL 0)
  message (FATAL_ERROR "RunFailingBuildTest.cmake expects a command after --")
endif (_arguments EQUAL 0)

cmake_language (EVAL CODE "
  execute_process (COMMAND ${_command}
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _output)")

# Shows the output like a test run directly by CTest would.
message ("${_output}")

if (_result EQUAL 0)
  message (FATAL_ERROR "The command succeeded although it must fail with "
    "output matching '${EXPECTED}'")
endif (_result EQUAL 0)

if (NOT _output MATCHES "${EXPECTED}")
  message (FATAL_ERROR "The command failed with exit status ${_result} as "
    "expected, but its output does not match '${EXPECTED}'")
endif (NOT _output MATCHES "${EXPECTED}")
