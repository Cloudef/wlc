find_package(GLESv2)
set_package_properties(GLESv2 PROPERTIES TYPE REQUIRED PURPOSE "Enables OpenGL renderer")

if (GLESv2_FOUND)
   add_definitions(${GLESv2_DEFINITIONS})
   include_directories(${GLESv2_INCLUDE_DIRS})

   add_library(wlc-renderer-gles2 SHARED gles2.c)
   set_target_properties(wlc-renderer-gles2 PROPERTIES PREFIX "")
   target_link_libraries(wlc-renderer-gles2 ${GLESv2_LIBRARIES})

   install(TARGETS wlc-renderer-gles2 DESTINATION lib/wlc)

   message("Building with gles2 renderer")
else ()
   message("Building without gles2 renderer")
endif ()

