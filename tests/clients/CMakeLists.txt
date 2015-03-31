set(tests
   fullscreen)

include_directories(${WAYLAND_CLIENT_INCLUDE_DIR})
foreach (test ${tests})
   add_executable(${test}_test ${test}.c)
   target_link_libraries(${test}_test ${WAYLAND_CLIENT_LIBRARIES} chck_wlc)
endforeach()
