set(backends
   "drm"
   "x11"
   )

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/backends")
foreach (backend ${backends})
   add_subdirectory(${backend})
endforeach ()
