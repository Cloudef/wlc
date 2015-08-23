find_package(Math REQUIRED)
find_package(Wayland REQUIRED)
find_package(Pixman REQUIRED)
find_package(XKBCommon REQUIRED)

# These are optional runtime (loaded dynamically)
# But are needed for compilation (headers)
find_package(X11 REQUIRED)
set_package_properties(X11 PROPERTIES TYPE REQUIRED PURPOSE "Enables X11 backend")
find_package(XCB REQUIRED COMPONENTS ewmh composite xkb)
set_package_properties(XCB PROPERTIES TYPE REQUIRED PURPOSE "Enables Xwayland and X11 backend")
find_package(GLESv2 REQUIRED)
set_package_properties(GLESv2 PROPERTIES TYPE REQUIRED PURPOSE "Enables OpenGL renderer")
find_package(EGL REQUIRED)
set_package_properties(EGL PROPERTIES TYPE REQUIRED PURPOSE "Enables EGL context")
find_package(DRM REQUIRED)
set_package_properties(DRM PROPERTIES TYPE REQUIRED PURPOSE "Enables DRM backend")
find_package(Udev REQUIRED)
set_package_properties(Udev PROPERTIES TYPE REQUIRED PURPOSE "Enables hotplugging")
find_package(LibInput REQUIRED)
set_package_properties(LibInput PROPERTIES TYPE REQUIRED PURPOSE "Enables input")

# Optional
find_package(Dbus)
set_package_properties(Dbus PROPERTIES TYPE RECOMMENDED PURPOSE "Enables logind support")
find_package(Systemd)
set_package_properties(Systemd PROPERTIES TYPE RECOMMENDED PURPOSE "Enables logind support")

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
   ${XCBCOMMON_INCLUDE_DIRS}
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

if (WLC_BUILD_STATIC)
   add_library(wlc STATIC ${sources})
else ()
   add_definitions(-DWLC_BUILD_SHARED)
   add_library(wlc SHARED ${sources})
endif ()

target_link_libraries(wlc
   PRIVATE
   wlc_protos
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
set(WLC_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/include" CACHE STRING "Include directories of wlc" FORCE)
set(WLC_LIBRARIES wlc CACHE STRING "Libraries needed for wlc" FORCE)
mark_as_advanced(WLC_DEFINITIONS WLC_INCLUDE_DIRS WLC_LIBRARIES)

# Add pkgconfig
configure_file(wlc.pc.in wlc.pc @ONLY)

# Install rules
install(TARGETS wlc DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/wlc" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/wlc.pc" DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/FindWlc.cmake" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/cmake/Modules")
