include_directories(${WLC_INCLUDE_DIRS} ${wlc_SOURCE_DIR}/lib/chck/src)
add_executable(example example.c)
target_link_libraries(example ${WLC_LIBRARY} ${WLC_LIBRARIES})
