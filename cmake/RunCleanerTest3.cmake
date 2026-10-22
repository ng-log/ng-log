# SPDX-FileCopyrightText: 2021-2024 Google Inc.
#
# SPDX-License-Identifier: BSD-3-Clause

set (RUNS 3)

# Create the subdirectory required by this unit test.
file (MAKE_DIRECTORY ${TEST_DIR}/${TEST_SUBDIR})

# A stale log planted after the first run must be removed by the remaining
# runs. This shows that the logs were written to and cleaned up in the
# directory the file count below examines. Finding no matching files alone
# cannot tell this apart from logs that were never written there.
set (STALE_LOG ${TEST_DIR}/${TEST_SUBDIR}test_cleanup_20000101-000000.1.relativefoo)

foreach (iter RANGE 1 ${RUNS})
  execute_process (COMMAND ${EMULATOR} ${LOGCLEANUP}
    RESULT_VARIABLE _RESULT)

  if (NOT _RESULT EQUAL 0)
    message (FATAL_ERROR "Failed to run logcleanup_unittest (error: ${_RESULT})")
  endif (NOT _RESULT EQUAL 0)

  if (iter EQUAL 1)
    file (WRITE ${STALE_LOG} "")
  endif (iter EQUAL 1)
endforeach (iter)

if (EXISTS ${STALE_LOG})
  message (SEND_ERROR "Expected stale log ${STALE_LOG} to be removed")
endif (EXISTS ${STALE_LOG})

# The unit test verifies that its log file exists in the subdirectory before it
# becomes overdue. Every log file of every run must be removed afterwards.
file (GLOB LOG_FILES ${TEST_DIR}/${TEST_SUBDIR}/*.relativefoo)
list (LENGTH LOG_FILES NUM_FILES)

if (TEST_ON_WINDOWS)
  # On Windows open files cannot be removed and will result in a permission
  # denied error while unlinking such file. Therefore, the log file of the
  # last run will be retained.
  set (_expected 1)
else (TEST_ON_WINDOWS)
  set (_expected 0)
endif (TEST_ON_WINDOWS)

if (NOT NUM_FILES EQUAL _expected)
  message (SEND_ERROR "Expected ${_expected} log files in directory "
    "${TEST_DIR}${TEST_SUBDIR} but found ${NUM_FILES}: ${LOG_FILES}")
endif (NOT NUM_FILES EQUAL _expected)

# Remove the subdirectory required by this unit test.
file (REMOVE_RECURSE ${TEST_DIR}/${TEST_SUBDIR})
