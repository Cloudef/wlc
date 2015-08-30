set(contexts
   "egl"
   )

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/context")
foreach (context ${contexts})
   add_subdirectory(${context})
endforeach ()
