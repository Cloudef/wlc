CMAKE_MINIMUM_REQUIRED(VERSION 3.1)
PROJECT(wlc VERSION 0.0.10 LANGUAGES C)
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${PROJECT_SOURCE_DIR}/CMake")

# Subprojects
include(subproject)
add_subdirectory(lib)

# CPack
set(CPACK_SYSTEM_NAME "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "7Z")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/pkg")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Wayland compositor library")
set(CPACK_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION ON)

# Includes
include(CheckFunctionExists)
include(GNUInstallDirs)
include(FeatureSummary)
include(CPack)
include(CTest)
include(test)

# Options
OPTION(WLC_X11_BACKEND_SUPPORT "Build X11 backend" ON)
OPTION(WLC_XWAYLAND_SUPPORT "Support XWayland applications" ON)
OPTION(WLC_WAYLAND_BACKEND_SUPPORT "Build Wayland backend" ON)
OPTION(WLC_BUILD_STATIC "Build wlc as static library" OFF)
OPTION(WLC_BUILD_EXAMPLES "Build wlc examples" ON)
OPTION(WLC_BUILD_TESTS "Build wlc tests" ON)

add_feature_info(X11Backend WLC_X11_BACKEND_SUPPORT "Compile X11 backend")
add_feature_info(XWaylandSupport WLC_XWAYLAND_SUPPORT "Compile support for XWayland")
add_feature_info(WaylandBackend WLC_WAYLAND_BACKEND_SUPPORT "Compile Wayland backend")
add_feature_info(Static WLC_BUILD_STATIC "Compile as static library")
add_feature_info(Examples WLC_BUILD_EXAMPLES "Compile example programs")
add_feature_info(Tests WLC_BUILD_TESTS "Compile tests")

# Find all required packages by various parts of the toolkit
find_package(Math REQUIRED)
find_package(Wayland REQUIRED)
find_package(Pixman REQUIRED)
find_package(XKBCommon REQUIRED)
find_package(Udev REQUIRED)
find_package(LibInput REQUIRED)
find_package(WaylandProtocols REQUIRED)

if (WLC_X11_BACKEND_SUPPORT)
   find_package(X11 REQUIRED COMPONENTS X11-xcb Xfixes)
   set_package_properties(X11 PROPERTIES TYPE REQUIRED PURPOSE "Enables X11 backend")
   find_package(XCB REQUIRED COMPONENTS xcb-ewmh xcb-composite xcb-xkb xcb-image xcb-xfixes)
   set_package_properties(XCB PROPERTIES TYPE REQUIRED PURPOSE "Enables Xwayland and X11 backend")
   if (X11_FOUND AND XCB_FOUND)
      set(ENABLE_X11_BACKEND YES)
   endif ()
endif ()

if (WLC_XWAYLAND_SUPPORT)
   if (NOT XCB_FOUND)
      find_package(XCB REQUIRED COMPONENTS xcb-ewmh xcb-composite xcb-xkb xcb-image xcb-xfixes)
      set_package_properties(XCB PROPERTIES TYPE REQUIRED PURPOSE "Enables Xwayland and X11 backend")
   endif ()
   if (XCB_FOUND)
      set(ENABLE_XWAYLAND_SUPPORT YES)
   endif ()
endif ()

find_package(GLESv2 REQUIRED)
set_package_properties(GLESv2 PROPERTIES TYPE REQUIRED PURPOSE "Enables OpenGL renderer")
find_package(EGL REQUIRED)
set_package_properties(EGL PROPERTIES TYPE REQUIRED PURPOSE "Enables EGL context")
find_package(DRM REQUIRED)
set_package_properties(DRM PROPERTIES TYPE REQUIRED PURPOSE "Enables DRM backend")
find_package(GBM REQUIRED)
set_package_properties(GBM PROPERTIES TYPE REQUIRED PURPOSE "Enables DRM backend")

# Optional
if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
   find_package(Dbus)
   set_package_properties(Dbus PROPERTIES TYPE RECOMMENDED PURPOSE "Enables logind support")
   find_package(Systemd)
   set_package_properties(Systemd PROPERTIES TYPE RECOMMENDED PURPOSE "Enables logind support")
   find_package(Elogind)
   set_package_properties(Elogind PROPERTIES TYPE RECOMMENDED PURPOSE "Enables logind support")
endif ()

if (NOT WLC_BUILD_STATIC)
   set(BUILD_SHARED_LIBS ON)
endif ()

# Compiler options
include(GCCCompatibleCompilerOptions)

if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
   set(ldflags -O1 --sort-common --as-needed -z,relro -z,now)
endif ()

check_c_compiler_flag(-fstack-protector-strong has_fstack_protector_strong)
if (has_fstack_protector_strong)
   list(APPEND cflags -fstack-protector-strong -fstack-check --param ssp-buffer-size=4)
else ()
   list(APPEND cflags -fstack-protector-all -fstack-check --param ssp-buffer-size=4)
endif ()

create_custom_linker_flags(Upstream ${ldflags})
create_custom_compiler_flags(Upstream -g -O2 ${cflags})

add_compiler_warnings(-Wall -Wextra -Wno-variadic-macros -Wno-long-long -Wformat=2 -Winit-self -Wfloat-equal -Wcast-align -Wpointer-arith -Wmissing-prototypes -Wno-nonnull-compare)

if (CMAKE_C_COMPILER_ID MATCHES "Clang")
   add_compiler_warnings(-Wno-pointer-bool-conversion -Wno-missing-field-initializers -Wno-missing-braces)
endif ()

# -std=c99 -fpic -fpie -D_DEFAULT_SOURCE
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_definitions(-D_DEFAULT_SOURCE)

check_function_exists(mkostemp mkostemp_exists)
if (mkostemp_exists)
   add_definitions(-D_GNU_SOURCE -DHAVE_MKOSTEMP=1)
else ()
   add_definitions(-D_DEFAULT_SOURCE -DHAVE_MKOSTEMP=0)
endif ()

check_function_exists(posix_fallocate posix_fallocate_exists)
if (posix_fallocate_exists)
   add_definitions(-DHAVE_POSIX_FALLOCATE=1)
endif ()

include_directories(shared)
add_subdirectory(protos)
add_subdirectory(src)

if (WLC_BUILD_EXAMPLES)
   add_subdirectory(example)
endif ()

if (WLC_BUILD_TESTS)
   add_subdirectory(tests)
endif ()

if ("${CMAKE_PROJECT_NAME}" STREQUAL "${PROJECT_NAME}")
   feature_summary(WHAT ALL)
endif ()
