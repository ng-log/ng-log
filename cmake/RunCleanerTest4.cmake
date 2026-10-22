# SPDX-FileCopyrightText: 2021-2024 Google Inc.
# SPDX-FileCopyrightText: 2026 The ng-log contributors
#
# SPDX-License-Identifier: BSD-3-Clause

set (RUNS 3)

# NOTE The log directory whose name contains non-ASCII characters is created
# by the unit test itself using the platform's Unicode filesystem API. The
# directory is matched using a wildcard instead of spelling out its name.

# A stale log planted after the first run must be removed by the remaining
# runs. This shows that the logs were written to and cleaned up in the
# directory the file count below examines. Finding no matching files alone
# cannot tell this apart from logs that were never written there.
foreach (iter RANGE 1 ${RUNS})
  execute_process (COMMAND ${EMULATOR} ${LOGCLEANUP}
    RESULT_VARIABLE _RESULT)

  if (NOT _RESULT EQUAL 0)
    message (FATAL_ERROR "Failed to run logcleanup_unittest (error: ${_RESULT})")
  endif (NOT _RESULT EQUAL 0)

  if (iter EQUAL 1)
    file (GLOB _LOG_DIRS ${TEST_DIR}/cleanup_*_dir)
    list (LENGTH _LOG_DIRS _NUM_LOG_DIRS)

    if (NOT _NUM_LOG_DIRS EQUAL 1)
      message (FATAL_ERROR "Expected 1 non-ASCII log directory but found ${_NUM_LOG_DIRS}")
    endif (NOT _NUM_LOG_DIRS EQUAL 1)

    set (STALE_LOG ${_LOG_DIRS}/test_cleanup_20000101-000000.1.nonasciifoo)
    file (WRITE ${STALE_LOG} "")
  endif (iter EQUAL 1)
endforeach (iter)

if (EXISTS ${STALE_LOG})
  message (SEND_ERROR "Expected stale log ${STALE_LOG} to be removed")
endif (EXISTS ${STALE_LOG})

# The unit test verifies that its log file exists in the directory before it
# becomes overdue. Every log file of every run must be removed afterwards.
file (GLOB LOG_FILES ${TEST_DIR}/cleanup_*_dir/*.nonasciifoo)
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
  message (SEND_ERROR "Expected ${_expected} log files in log directory "
    "but found ${NUM_FILES}: ${LOG_FILES}")
endif (NOT NUM_FILES EQUAL _expected)

# Remove the directory created by the unit test.
file (GLOB _LOG_DIRS ${TEST_DIR}/cleanup_*_dir)

if (_LOG_DIRS)
  file (REMOVE_RECURSE ${_LOG_DIRS})
endif (_LOG_DIRS)
