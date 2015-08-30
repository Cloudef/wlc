find_package(DRM)
set_package_properties(DRM PROPERTIES TYPE REQUIRED PURPOSE "Enables DRM backend")

if (DRM_FOUND)
   add_definitions(${GBM_DEFINITIONS} ${DRM_DEFINITIONS})
   include_directories(${GBM_INCLUDE_DIRS} ${DRM_INCLUDE_DIRS})

   add_library(wlc-backend-drm SHARED drm.c)
   set_target_properties(wlc-backend-drm PROPERTIES PREFIX "")
   target_link_libraries(wlc-backend-drm ${GBM_LIBRARIES} ${DRM_LIBRARIES})

   install(TARGETS wlc-backend-drm DESTINATION lib/wlc)

   message("Building with drm backend")
else ()
   message("Building without drm backend")
endif ()

