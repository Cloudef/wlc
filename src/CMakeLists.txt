SET(SRC
   compositor/compositor.c
   compositor/surface.c
   compositor/region.c
   compositor/buffer.c
   compositor/callback.c
   context/context.c
   context/egl.c
   context/x11/x11.c
   render/render.c
   render/gles2.c
   shell/shell.c
   shell/surface.c
   shell/xdg-shell.c
   shell/xdg-surface.c
   shell/xdg-shell-protocol.c
   types/string.c
   )

IF (UNIX)
   FIND_LIBRARY(DL_LIBRARY dl)
   MARK_AS_ADVANCED(DL_LIBRARY)
   IF (NOT DL_LIBRARY)
      MESSAGE(FATAL_ERROR "libdl is needed on unix systems")
   ENDIF ()
ENDIF (UNIX)

FIND_PACKAGE(Wayland REQUIRED)
FIND_PACKAGE(Pixman REQUIRED)

ADD_DEFINITIONS(-std=c99)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR} ${WAYLAND_INCLUDE_DIR} ${PIXMAN_INCLUDE_DIRS})
ADD_LIBRARY(wlc ${SRC})
TARGET_LINK_LIBRARIES(wlc ${WAYLAND_LIBRARIES} ${DL_LIBRARY})

SET(WLC_LIBRARIES ${WAYLAND_LIBRARIES} ${PIXMAN_LIBRARIES} ${DL_LIBRARY} CACHE STRING "Dependencies of wlc")
SET(WLC_INCLUDE_DIRS "${wlc_SOURCE_DIR}/include" CACHE STRING "Include directories of wlc")
