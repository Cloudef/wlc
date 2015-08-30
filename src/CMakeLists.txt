find_package(Math REQUIRED)
find_package(Wayland REQUIRED)
find_package(Pixman REQUIRED)
find_package(XKBCommon REQUIRED)
find_package(Udev REQUIRED)
find_package(LibInput REQUIRED)

add_subdirectory(platform/backend)
add_subdirectory(platform/context)
add_subdirectory(platform/render)
add_subdirectory(session/optional)
add_subdirectory(xwayland)

add_definitions(
   -DWL_HIDE_DEPRECATED
   ${WAYLAND_DEFINITIONS}
   ${PIXMAN_DEFINITIONS}
   ${XKBCOMMON_DEFINITIONS}
   ${UDEV_DEFINITIONS}
   ${LIBINPUT_DEFINITIONS}
   )

include_directories(
   ${CMAKE_CURRENT_SOURCE_DIR}
   ${PROJECT_BINARY_DIR}/protos
   ${PROJECT_SOURCE_DIR}/include
   ${CHCK_INCLUDE_DIRS}
   ${WAYLAND_SERVER_INCLUDE_DIRS}
   ${PIXMAN_INCLUDE_DIRS}
   ${XKBCOMMON_INCLUDE_DIRS}
   ${UDEV_INCLUDE_DIRS}
   ${LIBINPUT_INCLUDE_DIRS}
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
   platform/context/context.c
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
   )

foreach (src ${sources})
   set_source_files_properties(${src} PROPERTIES COMPILE_FLAGS -DWLC_FILE=\\\"${src}\\\")
endforeach ()

add_compile_options(-fvisibility=hidden)
add_library(wlc-object OBJECT ${sources})

if (WLC_BUILD_STATIC)
   add_library(wlc STATIC $<TARGET_OBJECTS:wlc-object>)
else ()
   add_definitions(-DWLC_BUILD_SHARED)
   add_library(wlc SHARED $<TARGET_OBJECTS:wlc-object>)
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

# Combine wlc-tests for tests, it's static so it has all symbols visible
add_library(wlc-tests STATIC $<TARGET_OBJECTS:wlc-object>)
target_link_libraries(wlc-tests
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
