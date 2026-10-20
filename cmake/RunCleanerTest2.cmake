# SPDX-FileCopyrightText: 2021-2024 Google Inc.
#
# SPDX-License-Identifier: BSD-3-Clause

set (RUNS 3)

# A stale log planted after the first run must be removed by the remaining
# runs. This shows that the logs were written to and cleaned up in the
# directory the file count below examines. Finding no matching files alone
# cannot tell this apart from logs that were never written there.
set (STALE_LOG ${TEST_DIR}test_cleanup_20000101-000000.1.barfoo)

# The log directory differs from the working directory, so that only logs
# written using the absolute prefix end up there.
file (MAKE_DIRECTORY ${TEST_DIR})

foreach (iter RANGE 1 ${RUNS})
  execute_process (COMMAND ${EMULATOR} ${LOGCLEANUP} --log_dir=${TEST_DIR}
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

file (GLOB LOG_FILES ${TEST_DIR}/test_cleanup_*.barfoo)
list (LENGTH LOG_FILES NUM_FILES)

if (WIN32)
  # On Windows open files cannot be removed and will result in a permission
  # denied error while unlinking such file. Therefore, the last file will be
  # retained.
  set (_expected 1)
 else (WIN32)
  set (_expected 0)
endif (WIN32)

if (NOT NUM_FILES EQUAL _expected)
  message (SEND_ERROR "Expected ${_expected} log file in log directory but found ${NUM_FILES}")
endif (NOT NUM_FILES EQUAL _expected)
