CMAKE_MINIMUM_REQUIRED(VERSION 3.1)
PROJECT(wlc VERSION 0.0.1 LANGUAGES C)
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
OPTION(WLC_BUILD_STATIC "Build wlc as static library" OFF)
OPTION(WLC_BUILD_EXAMPLES "Build wlc examples" ON)
OPTION(WLC_BUILD_TESTS "Build wlc tests" ON)

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

# Optional
find_package(Dbus)
set_package_properties(Dbus PROPERTIES TYPE RECOMMENDED PURPOSE "Enables logind support")
find_package(Systemd)
set_package_properties(Systemd PROPERTIES TYPE RECOMMENDED PURPOSE "Enables logind support")

if (NOT WLC_BUILD_STATIC)
   set(BUILD_SHARED_LIBS ON)
endif ()

# Compiler options
if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
   include(GCCCompatibleCompilerOptions)
   add_compiler_warnings(-Wall -Wextra -Wno-variadic-macros -Wno-long-long -Wformat=2 -Winit-self -Wfloat-equal -Wcast-align -Wpointer-arith -Wmissing-prototypes -Wsuggest-attribute=pure -Wsuggest-attribute=const)
   create_custom_linker_flags(Upstream -O1 --sort-common --as-needed -z,relro -z,now)
   check_c_compiler_flag(-fstack-protector-strong ok)
   if (ok)
      create_custom_compiler_flags(Upstream -g -O2 -fstack-protector-strong --param ssp-buffer-size=4 -fstack-check -flto)
   else ()
      create_custom_compiler_flags(Upstream -g -O2 -fstack-protector-all --param ssp-buffer-size=4 -fstack-check -flto)
   endif ()
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
