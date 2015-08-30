find_package(X11)
set_package_properties(X11 PROPERTIES TYPE REQUIRED PURPOSE "Enables X11 backend")
find_package(XCB COMPONENTS ewmh composite xkb)
set_package_properties(XCB PROPERTIES TYPE REQUIRED PURPOSE "Enables Xwayland and X11 backend")

if (X11_FOUND AND XCB_FOUND)
   add_definitions(${XCB_DEFINITIONS})
   include_directories(${XCB_INCLUDE_DIRS} ${X11_INCLUDE_DIR})

   add_library(wlc-backend-x11 SHARED x11.c)
   set_target_properties(wlc-backend-x11 PROPERTIES PREFIX "")
   target_link_libraries(wlc-backend-x11
      ${X11_LIBRARIES}
      ${X11_XCB_LIBRARIES}
      ${XCB_LIBRARIES_xkb})

   install(TARGETS wlc-backend-x11 DESTINATION lib/wlc)

   message("Building with X11 backend")
else ()
   message("Building without X11 backend")
endif ()

