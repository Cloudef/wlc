SET(SRC
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

IF (UNIX)
   FIND_LIBRARY(DL_LIBRARY dl)
   MARK_AS_ADVANCED(DL_LIBRARY)
   IF (NOT DL_LIBRARY)
      MESSAGE(FATAL_ERROR "libdl is needed on unix systems")
   ENDIF ()

   # FIXME: tests need visibility, build 2 libraries
   # ADD_DEFINITIONS(-fvisibility=hidden)
ENDIF (UNIX)

# Parse soversion version
STRING(REGEX MATCHALL "[0-9]+" VERSION_COMPONENTS ${WLC_VERSION})
LIST(GET VERSION_COMPONENTS 0 SOVERSION)

# Required
FIND_PACKAGE(Wayland REQUIRED)
FIND_PACKAGE(Pixman REQUIRED)
FIND_PACKAGE(XKBCommon REQUIRED)

# These are optional runtime (loaded dynamically)
# But are needed for compilation (headers)
FIND_PACKAGE(X11 REQUIRED)
FIND_PACKAGE(XCB REQUIRED COMPONENTS ewmh composite)
FIND_PACKAGE(GLESv2 REQUIRED)
FIND_PACKAGE(EGL REQUIRED)
FIND_PACKAGE(DRM REQUIRED)
FIND_PACKAGE(UDEV REQUIRED)
FIND_PACKAGE(libinput REQUIRED)

# Protocols
INCLUDE(Wayland)
WAYLAND_ADD_PROTOCOL_SERVER(proto-xdg-shell "${wlc_SOURCE_DIR}/protos/xdg-shell.xml" xdg-shell)
LIST(APPEND SRC ${proto-xdg-shell})

ADD_DEFINITIONS(-DWL_HIDE_DEPRECATED)
INCLUDE_DIRECTORIES(${wlc_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} ${WAYLAND_SERVER_INCLUDE_DIR} ${PIXMAN_INCLUDE_DIRS} ${GBM_INCLUDE_DIR} ${DRM_INCLUDE_DIR} ${XCBCOMMON_INCLUDE_DIR} ${EGL_INCLUDE_DIR} ${GLESv2_INCLUDE_DIR} ${UDEV_INCLUDE_DIR} ${LIBINPUT_INCLUDE_DIR} ${X11_INCLUDE_DIR} ${wlc_SOURCE_DIR}/lib/chck/src)

IF (WLC_BUILD_STATIC)
   ADD_LIBRARY(wlc STATIC ${SRC})
ELSE ()
   ADD_DEFINITIONS(-DWLC_BUILD_SHARED)
   ADD_LIBRARY(wlc SHARED ${SRC})
ENDIF ()

SET_TARGET_PROPERTIES(wlc PROPERTIES
   VERSION ${WLC_VERSION}
   SOVERSION ${SOVERSION})
TARGET_LINK_LIBRARIES(wlc ${WAYLAND_SERVER_LIBRARIES} ${PIXMAN_LIBRARIES} ${XKBCOMMON_LIBRARIES} ${LIBINPUT_LIBRARIES} ${UDEV_LIBRARIES} ${DL_LIBRARY} chck_wlc)

SET(WLC_LIBRARY wlc CACHE STRING "wlc library" FORCE)
SET(WLC_INCLUDE_DIRS "${wlc_SOURCE_DIR}/include" CACHE STRING "Include directories of wlc" FORCE)
SET(WLC_LIBRARIES ${WAYLAND_SERVER_LIBRARIES} ${PIXMAN_LIBRARIES} ${XKBCOMMON_LIBRARIES} ${DL_LIBRARY} CACHE STRING "Dependencies of wlc" FORCE)

# Install rules
INSTALL(TARGETS wlc DESTINATION lib)
INSTALL(DIRECTORY "${wlc_SOURCE_DIR}/include" DESTINATION include)
