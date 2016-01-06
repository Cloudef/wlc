add_definitions(
   -DWL_HIDE_DEPRECATED
   ${WAYLAND_DEFINITIONS}
   ${PIXMAN_DEFINITIONS}
   ${GBM_DEFINITIONS}
   ${DRM_DEFINITIONS}
   ${XKBCOMMON_DEFINITIONS}
   ${EGL_DEFINITIONS}
   ${GLESv2_DEFINITIONS}
   ${UDEV_DEFINITIONS}
   ${LIBINPUT_DEFINITIONS}
   ${XCB_DEFINITIONS}
   -DXWAYLAND_EXECUTABLE="${XWAYLAND_EXECUTABLE}"
   )

include_directories(
   ${CMAKE_CURRENT_SOURCE_DIR}
   ${PROJECT_BINARY_DIR}/protos
   ${PROJECT_SOURCE_DIR}/include
   ${CHCK_INCLUDE_DIRS}
   ${WAYLAND_SERVER_INCLUDE_DIRS}
   ${PIXMAN_INCLUDE_DIRS}
   ${GBM_INCLUDE_DIRS}
   ${DRM_INCLUDE_DIRS}
   ${XKBCOMMON_INCLUDE_DIRS}
   ${EGL_INCLUDE_DIRS}
   ${GLESv2_INCLUDE_DIRS}
   ${UDEV_INCLUDE_DIRS}
   ${LIBINPUT_INCLUDE_DIRS}
   ${XCB_INCLUDE_DIRS}
   ${X11_INCLUDE_DIR}
   )

set(sources
   compositor/compositor.c
   compositor/output.c
   compositor/seat/data.c
   compositor/seat/keyboard.c
   compositor/seat/keymap.c
   compositor/seat/pointer.c
   compositor/seat/seat.c
   compositor/seat/touch.c
   compositor/shell/shell.c
   compositor/shell/xdg-shell.c
   compositor/view.c
   platform/backend/backend.c
   platform/backend/drm.c
   platform/backend/x11.c
   platform/context/context.c
   platform/context/egl.c
   platform/render/gles2.c
   platform/render/render.c
   resources/resources.c
   resources/types/buffer.c
   resources/types/data-source.c
   resources/types/region.c
   resources/types/shell-surface.c
   resources/types/surface.c
   resources/types/xdg-surface.c
   session/fd.c
   session/tty.c
   session/udev.c
   wlc.c
   xwayland/xwayland.c
   xwayland/xwm.c
   wayland/wayland.c
   )

if (DBUS_FOUND)
   add_definitions(-DDBUS_DISABLE_DEPRECATED ${DBUS_DEFINITIONS})
   include_directories(${DBUS_INCLUDE_DIRS})
   list(APPEND sources session/dbus.c)
endif ()

if (SYSTEMD_FOUND)
   add_definitions(${SYSTEMD_DEFINITIONS})
   include_directories(${SYSTEMD_INCLUDE_DIRS})

   if (DBUS_FOUND)
      add_definitions(-DHAS_LOGIND)
      list(APPEND sources session/logind.c)
   else ()
      message("Dbus was not found, so logind is disabled")
   endif ()
endif ()

foreach (src ${sources})
   set_source_files_properties(${src} PROPERTIES COMPILE_FLAGS -DWLC_FILE=\\\"${src}\\\")
endforeach ()

add_compile_options(-fvisibility=hidden)
add_library(wlc-object OBJECT ${sources})
add_dependencies(wlc-object wlc-protos)

if (WLC_BUILD_STATIC)
   add_library(wlc STATIC $<TARGET_OBJECTS:wlc-object>)
else ()
   add_definitions(-DWLC_BUILD_SHARED)
   add_library(wlc SHARED $<TARGET_OBJECTS:wlc-object>)
endif ()

target_link_libraries(wlc
   PRIVATE
   wlc-protos
   ${CHCK_LIBRARIES}
   ${WAYLAND_SERVER_LIBRARIES}
   ${PIXMAN_LIBRARIES}
   ${XKBCOMMON_LIBRARIES}
   ${LIBINPUT_LIBRARIES}
   ${UDEV_LIBRARIES}
   ${MATH_LIBRARY}
   ${CMAKE_DL_LIBS}
   )

# Combine wlc-tests for tests, it's static so it has all symbols visible
add_library(wlc-tests STATIC $<TARGET_OBJECTS:wlc-object>)
target_link_libraries(wlc-tests
   PRIVATE
   wlc-protos
   ${CHCK_LIBRARIES}
   ${WAYLAND_SERVER_LIBRARIES}
   ${PIXMAN_LIBRARIES}
   ${XKBCOMMON_LIBRARIES}
   ${LIBINPUT_LIBRARIES}
   ${UDEV_LIBRARIES}
   ${MATH_LIBRARY}
   ${CMAKE_DL_LIBS}
   )

# Parse soversion
string(REGEX MATCHALL "[0-9]+" VERSION_COMPONENTS ${PROJECT_VERSION})
list(GET VERSION_COMPONENTS 0 SOVERSION)
set_target_properties(wlc PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${SOVERSION})

# Set helpful variables for add_subdirectory build
set(WLC_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/include" ${XKBCOMMON_INCLUDE_DIRS} ${LIBINPUT_INCLUDE_DIRS} CACHE STRING "Include directories of wlc" FORCE)
set(WLC_LIBRARIES wlc ${XKBCOMMON_LIBRARIES} ${LIBINPUT_LIBRARIES} CACHE STRING "Libraries needed for wlc" FORCE)
mark_as_advanced(WLC_DEFINITIONS WLC_INCLUDE_DIRS WLC_LIBRARIES)

# Add pkgconfig
configure_file(wlc.pc.in wlc.pc @ONLY)

# Install rules
install(TARGETS wlc DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/wlc" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/wlc.pc" DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/FindWlc.cmake" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/cmake/Modules")
