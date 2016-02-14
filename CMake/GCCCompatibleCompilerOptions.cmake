include(CheckCCompilerFlag)

# Add list of compiler warnings
# Every warning gets checked with check_c_compiler_flags
function(add_compiler_warnings)
   foreach (flag ${ARGN})
      check_c_compiler_flag(${flag} ok)
      if (ok)
         add_compile_options(${flag})
      endif ()
   endforeach ()
endfunction ()

# Create new ${EXE,MODULE,SHARED}_LINKER_FLAGS build type for list of linker flags
# Every linker flag gets checked with check_c_compiler_flag
function(create_custom_linker_flags name)
   set(ldflags)
   foreach (flag ${ARGN})
      check_c_compiler_flag(-Wl,${flag} ok)
      if (ok)
         if (ldflags)
            set(ldflags "${ldflags},${flag}")
         else ()
            set(ldflags "-Wl,${flag}")
         endif ()
      endif ()
   endforeach ()

   string(TOUPPER ${name} upper)
   set(CMAKE_EXE_LINKER_FLAGS_${upper} "${ldflags}" CACHE STRING "${name} exe linker flags" FORCE)
   set(CMAKE_MODULE_LINKER_FLAGS_${upper} "${ldflags}" CACHE STRING "${name} module linker flags" FORCE)
   set(CMAKE_SHARED_LINKER_FLAGS_${upper} "${ldflags}" CACHE STRING "${name} shared linker flags" FORCE)
   mark_as_advanced(CMAKE_EXE_LINKER_FLAGS_${upper} CMAKE_SHARED_LINKER_FLAGS_${upper} CMAKE_MODULE_LINKER_FLAGS_${upper})
endfunction ()

# Create new {C,CXX}_FLAGS build type for list of compiler flags
# Every compiler flag gets checked with check_c_compiler_flag
function(create_custom_compiler_flags name)
   set(cflags)
   foreach (flag ${ARGN})
      check_c_compiler_flag(${flag} ok)
      if (ok)
         if (cflags)
            set(cflags "${cflags} ${flag}")
         else ()
            set(cflags "${flag}")
         endif ()
      endif ()
   endforeach ()

   string(TOUPPER ${name} upper)
   set(CMAKE_C_FLAGS_${upper} "${cflags}" CACHE STRING "${name} C flags" FORCE)
   set(CMAKE_CXX_FLAGS_${upper} "${cflags}" CACHE STRING "${name} CXX flags" FORCE)
   mark_as_advanced(CMAKE_CXX_FLAGS_${upper} CMAKE_C_FLAGS_${upper})
endfunction ()
