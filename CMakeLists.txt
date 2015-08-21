CMAKE_MINIMUM_REQUIRED(VERSION 2.8)
PROJECT(wlc C)
set(WLC_NAME "wlc")
set(WLC_DESCRIPTION "Wayland compositor library")
set(WLC_VERSION "0.0.1")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${wlc_SOURCE_DIR}/CMake)
include(CTest)
include(test)

# Options
OPTION(WLC_BUILD_STATIC "Build wlc as static library" OFF)
OPTION(WLC_BUILD_EXAMPLES "Build wlc examples" ON)
OPTION(WLC_BUILD_TESTS "Build wlc tests" ON)

# Warnings
if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
   add_definitions(
      -Wall -Wextra -Wno-variadic-macros -Wno-long-long -Wformat=2 -Winit-self -Wfloat-equal
      -Wsuggest-attribute=pure -Wsuggest-attribute=const)
endif ()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if (${CMAKE_VERSION} VERSION_LESS 2.8.9)
   add_definitions(-fPIC)
endif ()

add_definitions(-std=c99 -D_DEFAULT_SOURCE)
include_directories(shared)

add_subdirectory(lib)
add_subdirectory(src)

if (WLC_BUILD_EXAMPLES)
   add_subdirectory(example)
endif ()

if (WLC_BUILD_TESTS)
   add_subdirectory(tests)
endif ()
