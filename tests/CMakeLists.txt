set(tests
   resources)

   # FIXME: disabling compositor tests until we have headless backend
   # wl-extension
   # fullscreen)

include_directories(
   ${PROJECT_SOURCE_DIR}/src
   ${PROJECT_BINARY_DIR}/protos
   ${WAYLAND_SERVER_INCLUDE_DIRS}
   ${WAYLAND_CLIENT_INCLUDE_DIRS}
   ${XKBCOMMON_INCLUDE_DIRS}
   ${WLC_INCLUDE_DIRS}
   ${CHCK_INCLUDE_DIRS}
)
foreach (test ${tests})
   set_source_files_properties(${test}.c PROPERTIES COMPILE_FLAGS -DWLC_FILE="\\\"${test}.c\\\"")
   add_executable(${test}-test ${test}.c)
   target_link_libraries(${test}-test PRIVATE wlc-tests wlc-tests-protos ${WAYLAND_SERVER_LIBRARIES} ${WAYLAND_CLIENT_LIBRARIES})
   add_test_ex(${test}-test)
endforeach()
