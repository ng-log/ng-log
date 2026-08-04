set (RUNS 3)

# NOTE The log directory whose name contains non-ASCII characters is created
# by the unit test itself using the platform's Unicode filesystem API. The
# directory is matched using a wildcard instead of spelling out its name.

foreach (iter RANGE 1 ${RUNS})
  execute_process (COMMAND ${LOGCLEANUP} -log_dir=${TEST_DIR}
    RESULT_VARIABLE _RESULT)

  if (NOT _RESULT EQUAL 0)
    message (FATAL_ERROR "Failed to run logcleanup_unittest (error: ${_RESULT})")
  endif (NOT _RESULT EQUAL 0)
endforeach (iter)

file (GLOB LOG_FILES ${TEST_DIR}/cleanup_*_dir/test_cleanup_*.nonasciifoo)
list (LENGTH LOG_FILES NUM_FILES)

if (TEST_ON_WINDOWS)
  # On Windows open files cannot be removed and will result in a permission
  # denied error while unlinking such file. Therefore, the last file will be
  # retained.
  set (_expected 1)
 else (TEST_ON_WINDOWS)
  set (_expected 0)
endif (TEST_ON_WINDOWS)

if (NOT NUM_FILES EQUAL _expected)
  message (SEND_ERROR "Expected ${_expected} log file in log directory but found ${NUM_FILES}")
endif (NOT NUM_FILES EQUAL _expected)

# Remove the directory created by the unit test.
file (GLOB _LOG_DIRS ${TEST_DIR}/cleanup_*_dir)

if (_LOG_DIRS)
  file (REMOVE_RECURSE ${_LOG_DIRS})
endif (_LOG_DIRS)
