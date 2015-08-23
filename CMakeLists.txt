CMAKE_MINIMUM_REQUIRED(VERSION 3.0)
PROJECT(wlc VERSION 0.0.1 LANGUAGES C)
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${PROJECT_SOURCE_DIR}/CMake")

# Subprojects
include(subproject)
add_subdirectory(lib)

# CPack
set(CPACK_GENERATOR "7Z")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/pkg")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Wayland compositor library")
set(CPACK_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION ON)

# Includes
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

if (NOT WLC_BUILD_STATIC)
   set(BUILD_SHARED_LIBS ON)
endif ()

# Warnings
if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
   set(ldflags -Wl,-O1,--no-undefined,--sort-common,--as-needed,-z,relro,-z,now)
   set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${ldflags}")
   set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${ldflags}")
   set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${ldflags}")

   if ("${CMAKE_C_FLAGS}" STREQUAL "")
      add_definitions(-O2)
      add_definitions(-fstack-protector-strong -fstack-check -flto)
   endif ()

   add_definitions(
      -Wall -Wextra -Wno-variadic-macros -Wno-long-long -Wformat=2 -Winit-self -Wfloat-equal
      -Wcast-align -Wpointer-arith -Wmissing-prototypes -Wsuggest-attribute=pure -Wsuggest-attribute=const)
endif ()

# -std=c99 -fpic -fpie -D_DEFAULT_SOURCE
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_definitions(-D_DEFAULT_SOURCE)

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
