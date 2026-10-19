# SPDX-FileCopyrightText: 2026 The ng-log contributors
#
# SPDX-License-Identifier: BSD-3-Clause

# The process flag registry is set up during static initialization, so the
# temporary directory must be chosen before the test process starts. The tests
# run once with an empty temporary directory, which must stay empty, and once
# with a temporary directory that does not exist.

file (REMOVE_RECURSE ${TEST_DIR})
file (MAKE_DIRECTORY ${TEST_DIR})

set (ENV{TMPDIR} ${TEST_DIR})
execute_process (COMMAND ${EMULATOR} ${FLAGS_UNITTEST} --gtest_filter=${FILTER}
  RESULT_VARIABLE _result)

if (NOT _result EQUAL 0)
  message (FATAL_ERROR
    "Expected exit code 0 with an empty temporary directory but got ${_result}")
endif (NOT _result EQUAL 0)

file (GLOB _remaining LIST_DIRECTORIES true ${TEST_DIR}/* ${TEST_DIR}/.*)

if (_remaining)
  message (FATAL_ERROR
    "Expected an empty temporary directory but found ${_remaining}")
endif (_remaining)

set (ENV{TMPDIR} ${TEST_DIR}/nonexistent)
execute_process (COMMAND ${EMULATOR} ${FLAGS_UNITTEST} --gtest_filter=${FILTER}
  RESULT_VARIABLE _result)

if (NOT _result EQUAL 0)
  message (FATAL_ERROR
    "Expected exit code 0 with a nonexistent temporary directory but got ${_result}")
endif (NOT _result EQUAL 0)

file (REMOVE_RECURSE ${TEST_DIR})
