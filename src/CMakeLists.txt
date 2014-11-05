SET(SRC
   backend/backend.c
   backend/drm.c
   backend/udev/udev.c
   backend/x11.c
   compositor/buffer.c
   compositor/callback.c
   compositor/compositor.c
   compositor/output.c
   compositor/region.c
   compositor/surface.c
   compositor/view.c
   context/context.c
   context/egl.c
   data-device/manager.c
   render/gles2.c
   render/render.c
   seat/client.c
   seat/keyboard.c
   seat/keymap.c
   seat/pointer.c
   seat/seat.c
   session/fd.c
   session/tty.c
   shell/shell.c
   shell/surface.c
   shell/xdg-popup.c
   shell/xdg-shell.c
   shell/xdg-surface.c
   types/string.c
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

   SET(CMAKE_POSITION_INDEPENDENT_CODE ON)
   IF (${CMAKE_VERSION} VERSION_LESS 2.8.9)
      ADD_DEFINITIONS(-fPIC)
   ENDIF ()

   ADD_DEFINITIONS(-fvisibility=hidden)
ENDIF (UNIX)

# Math lib
FIND_LIBRARY(MATH_LIBRARY m)
MARK_AS_ADVANCED(MATH_LIBRARY)
IF (NOT MATH_LIBRARY)
   MESSAGE(FATAL_ERROR "Math library was not found")
ENDIF ()

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

ADD_DEFINITIONS(-std=c99 -D_DEFAULT_SOURCE)
INCLUDE_DIRECTORIES(${wlc_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} ${WAYLAND_SERVER_INCLUDE_DIR} ${PIXMAN_INCLUDE_DIRS} ${GBM_INCLUDE_DIR} ${DRM_INCLUDE_DIR} ${XCBCOMMON_INCLUDE_DIR} ${EGL_INCLUDE_DIR} ${GLESv2_INCLUDE_DIR} ${UDEV_INCLUDE_DIR} ${LIBINPUT_INCLUDE_DIR} ${X11_INCLUDE_DIR})

IF (WLC_BUILD_STATIC)
   ADD_LIBRARY(wlc STATIC ${SRC})
ELSE ()
   ADD_DEFINITIONS(-DWLC_BUILD_SHARED)
   ADD_LIBRARY(wlc SHARED ${SRC})
ENDIF ()

SET_TARGET_PROPERTIES(wlc PROPERTIES
   VERSION ${WLC_VERSION}
   SOVERSION ${SOVERSION})
TARGET_LINK_LIBRARIES(wlc ${WAYLAND_SERVER_LIBRARIES} ${PIXMAN_LIBRARIES} ${XKBCOMMON_LIBRARIES} ${LIBINPUT_LIBRARIES} ${UDEV_LIBRARIES} ${DL_LIBRARY})

SET(WLC_LIBRARY wlc CACHE STRING "wlc library" FORCE)
SET(WLC_INCLUDE_DIRS "${wlc_SOURCE_DIR}/include" CACHE STRING "Include directories of wlc" FORCE)
SET(WLC_LIBRARIES ${WAYLAND_SERVER_LIBRARIES} ${PIXMAN_LIBRARIES} ${XKBCOMMON_LIBRARIES} ${DL_LIBRARY} ${MATH_LIBRARY} CACHE STRING "Dependencies of wlc" FORCE)

# Install rules
INSTALL(TARGETS wlc DESTINATION lib)
INSTALL(FILES "${wlc_SOURCE_DIR}/include/wlc.h" DESTINATION include)
