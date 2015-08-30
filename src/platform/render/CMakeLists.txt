set(renderers
   "gles2"
   )

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/render")
foreach (renderer ${renderers})
   add_subdirectory(${renderer})
endforeach ()
