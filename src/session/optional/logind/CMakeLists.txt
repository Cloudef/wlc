find_package(Systemd)
set_package_properties(Systemd PROPERTIES TYPE RECOMMENDED PURPOSE "Enables logind support")

if (SYSTEMD_FOUND)
   add_definitions(${SYSTEMD_DEFINITIONS})
   include_directories(${SYSTEMD_INCLUDE_DIRS})

   add_library(wlc-optional-logind SHARED logind.c)
   set_target_properties(wlc-optional-logind PROPERTIES PREFIX "")
   target_link_libraries(wlc-optional-logind ${SYSTEMD_LIBRARIES})

   install(TARGETS wlc-optional-logind DESTINATION lib/wlc)

   message("Building with logind support")
else ()
   message("Building without logind support")
endif ()
