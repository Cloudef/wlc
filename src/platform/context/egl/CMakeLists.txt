find_package(EGL)
set_package_properties(EGL PROPERTIES TYPE REQUIRED PURPOSE "Enables EGL context")

if (EGL_FOUND)
   add_definitions(${EGL_DEFINITIONS})
   include_directories(${EGL_INCLUDE_DIRS})

   add_library(wlc-context-egl SHARED egl.c)
   set_target_properties(wlc-context-egl PROPERTIES PREFIX "")
   target_link_libraries(wlc-context-egl ${EGL_LIBRARIES})
   install(TARGETS wlc-context-egl DESTINATION lib/wlc)

   message("Building with EGL context")
else ()
   message("Building without EGL context")
endif ()

