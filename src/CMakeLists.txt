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

foreach (src ${sources})
   set_source_files_properties(${src} PROPERTIES COMPILE_FLAGS -DWLC_FILE=\\\"${src}\\\")
endforeach ()

if (UNIX)
   find_library(DL_LIBRARY dl)
   mark_as_advanced(DL_LIBRARY)
   if (NOT DL_LIBRARY)
      message(FATAL_ERROR "libdl is needed on unix systems")
   endif ()

   # FIXME: tests need visibility, build 2 libraries
   # ADD_DEFINITIONS(-fvisibility=hidden)
endif (UNIX)

find_library(MATH_LIBRARY m)
mark_as_advanced(MATH_LIBRARY)
if (NOT MATH_LIBRARY)
   message(FATAL_ERROR "libm is needed")
endif ()

# Parse soversion version
string(REGEX MATCHALL "[0-9]+" VERSION_COMPONENTS ${WLC_VERSION})
list(GET VERSION_COMPONENTS 0 SOVERSION)

# Required
find_package(Wayland REQUIRED)
find_package(Pixman REQUIRED)
find_package(XKBCommon REQUIRED)

# These are optional runtime (loaded dynamically)
# But are needed for compilation (headers)
find_package(X11 REQUIRED)
find_package(XCB REQUIRED COMPONENTS ewmh composite xkb)
find_package(GLESv2 REQUIRED)
find_package(EGL REQUIRED)
find_package(DRM REQUIRED)
find_package(UDEV REQUIRED)
find_package(libinput REQUIRED)

# For logind support
find_package(Dbus)
find_package(Systemd)

# Protocols
include(Wayland)
wayland_add_protocol_server(proto-xdg-shell "${wlc_SOURCE_DIR}/protos/xdg-shell.xml" xdg-shell)
list(APPEND sources ${proto-xdg-shell})

set(definitions
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
   ${CMAKE_CURRENT_BINARY_DIR}
   ${wlc_SOURCE_DIR}/include
   ${wlc_SOURCE_DIR}/lib/chck/src
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

if (DBUS_FOUND AND SYSTEMD_FOUND)
   message("Building with logind support")
   include_directories(${DBUS_INCLUDE_DIRS} ${SYSTEMD_INCLUDE_DIRS})
   list(APPEND definitions -DHAS_LOGIND -DDBUS_DISABLE_DEPRECATED ${DBUS_DEFINITIONS} ${SYSTEMD_DEFINITIONS})
   list(APPEND sources session/logind.c session/dbus.c)
endif ()

if (WLC_BUILD_STATIC)
   add_library(wlc STATIC ${sources})
else ()
   add_definitions(-DWLC_BUILD_SHARED)
   add_library(wlc SHARED ${sources})
endif ()

set_target_properties(wlc PROPERTIES VERSION ${WLC_VERSION} SOVERSION ${SOVERSION})

add_definitions(${definitions})
target_link_libraries(wlc
   chck_wlc
   ${WAYLAND_SERVER_LIBRARIES}
   ${PIXMAN_LIBRARIES}
   ${XKBCOMMON_LIBRARIES}
   ${LIBINPUT_LIBRARIES}
   ${UDEV_LIBRARIES}
   ${DL_LIBRARY}
   )

# Set helpful variables for add_subdirectory build
set(WLC_DEFINITIONS "${definitions}" CACHE STRING "Compile definitions of wlc" FORCE)
set(WLC_INCLUDE_DIRS "${wlc_SOURCE_DIR}/include" CACHE STRING "Include directories of wlc" FORCE)
set(WLC_LIBRARIES wlc ${WAYLAND_SERVER_LIBRARIES} ${PIXMAN_LIBRARIES} ${XKBCOMMON_LIBRARIES} ${DL_LIBRARY} ${MATH_LIBRARY} CACHE STRING "Libraries needed for wlc" FORCE)

# Add pkgconfig
configure_file(wlc.pc.in wlc.pc @ONLY)

# Install rules
install(TARGETS wlc DESTINATION lib)
install(DIRECTORY "${wlc_SOURCE_DIR}/include/wlc" DESTINATION include)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/wlc.pc" DESTINATION lib/pkgconfig)
