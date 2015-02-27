# Wraps add_test
# Fixes win32 .exe extensions on mingw at least
# Uses CTEST_EXEC_WITH variable which you can set to for example valgrind to run tests with valgrind.
# Also uses CTEST_OUTPUT_DIRECTORY to set global output directory

function(add_test_ex target)
   if (WIN32)
      add_test(${target} ${CTEST_EXEC_WITH} "${CTEST_OUTPUT_DIRECTORY}/${target}.exe")
   else ()
      add_test(${target} ${CTEST_EXEC_WITH} "${CTEST_OUTPUT_DIRECTORY}/${target}")
   endif ()
endfunction()
