set(optionals
   "dbus"
   "logind"
   )

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/optional")
foreach (optional ${optionals})
   add_subdirectory(${optional})
endforeach ()
