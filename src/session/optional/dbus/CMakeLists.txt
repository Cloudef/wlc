find_package(Dbus)
set_package_properties(Dbus PROPERTIES TYPE RECOMMENDED PURPOSE "Enables logind support")

if (DBUS_FOUND)
   add_definitions(-DDBUS_DISABLE_DEPRECATED ${DBUS_DEFINITIONS})
   include_directories(${DBUS_INCLUDE_DIRS})

   add_library(wlc-optional-dbus SHARED dbus.c)
   set_target_properties(wlc-optional-dbus PROPERTIES PREFIX "")
   target_link_libraries(wlc-optional-dbus ${DBUS_LIBRARIES})

   install(TARGETS wlc-optional-dbus DESTINATION lib/wlc)

   message("Building with dbus support")
else ()
   message("Building without dbus support")
endif ()
