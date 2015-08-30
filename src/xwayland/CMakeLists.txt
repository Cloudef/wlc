find_package(XCB COMPONENTS ewmh composite xkb)
set_package_properties(XCB PROPERTIES TYPE REQUIRED PURPOSE "Enables Xwayland and X11 backend")

if (XCB_FOUND)
   add_definitions(${XCB_DEFINITIONS})
   include_directories(${XCB_INCLUDE_DIRS})

   add_library(wlc-xwayland SHARED xwayland.c xwm.c)
   set_target_properties(wlc-xwayland PROPERTIES PREFIX "")
   target_link_libraries(wlc-xwayland
      ${XCB_LIBRARIES_composite}
      ${XCB_LIBRARIES_xfixes})

   install(TARGETS wlc-xwayland DESTINATION lib/wlc)

   message("Building with Xwayland support")
else ()
   message("Building without Xwayland support")
endif ()

