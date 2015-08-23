set(tests
   resources)

include_directories(${PROJECT_SOURCE_DIR}/src ${WLC_INCLUDE_DIRS} ${CHCK_INCLUDE_DIRS})
foreach (test ${tests})
   set_source_files_properties(${test}.c PROPERTIES COMPILE_FLAGS -DWLC_FILE="\\\"${test}.c\\\"")
   add_executable(${test}_test ${test}.c)
   target_link_libraries(${test}_test PRIVATE ${WLC_LIBRARIES})
   add_test_ex(${test}_test)
endforeach()

add_subdirectory(clients)
