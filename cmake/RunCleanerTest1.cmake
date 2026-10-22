# SPDX-FileCopyrightText: 2021-2024 Google Inc.
# SPDX-FileCopyrightText: 2026 The ng-log contributors
#
# SPDX-License-Identifier: BSD-3-Clause

set (RUNS 3)

foreach (iter RANGE 1 ${RUNS})
  set (ENV{NGLOG_DIR} ${TEST_DIR})
  execute_process (COMMAND ${EMULATOR} ${LOGCLEANUP} RESULT_VARIABLE _RESULT)

  if (NOT _RESULT EQUAL 0)
    message (FATAL_ERROR "Failed to run logcleanup_unittest (error: ${_RESULT})")
  endif (NOT _RESULT EQUAL 0)
endforeach (iter)

# The unit test verifies that its INFO, WARNING and ERROR log files exist in the
# log directory before they become overdue. Every log file of every run must be
# removed afterwards.
file (GLOB LOG_FILES ${TEST_DIR}/*.foobar)
list (LENGTH LOG_FILES NUM_FILES)

if (TEST_ON_WINDOWS)
  # On Windows open files cannot be removed and will result in a permission
  # denied error while unlinking such file. Therefore, the INFO, WARNING and
  # ERROR log files of the last run will be retained.
  set (_expected 3)
else (TEST_ON_WINDOWS)
  set (_expected 0)
endif (TEST_ON_WINDOWS)

if (NOT NUM_FILES EQUAL _expected)
  message (SEND_ERROR "Expected ${_expected} log files in log directory "
    "${TEST_DIR} but found ${NUM_FILES}: ${LOG_FILES}")
endif (NOT NUM_FILES EQUAL _expected)
