# - Find Pixman
# Find the Pixman libraries
#
#  This module defines the following variables:
#     PIXMAN_FOUND        - True if pixman is found
#     PIXMAN_LIBRARIES    - Pixman libraries
#     PIXMAN_INCLUDE_DIRS - Pixman include dirs
#	  PIXMAN_DEFINITIONS  - Compiler switches required for using pixman
#

find_package(PkgConfig)
pkg_check_modules(PC_PIXMAN QUIET pixman-1)
find_library(PIXMAN_LIBRARIES NAMES pixman-1 HINTS ${PC_PIXMAN_LIBRARY_DIRS})
find_path(PIXMAN_INCLUDE_DIRS NAMES pixman.h PATH_SUFFIXES pixman-1 HINTS ${PC_PIXMAN_INCLUDE_DIRS})

set(PIXMAN_DEFINITIONS ${PC_PIXMAN_CFLAGS_OTHER})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(pixman-1 DEFAULT_MSG PIXMAN_LIBRARIES PIXMAN_INCLUDE_DIRS)
mark_as_advanced(PIXMAN_INCLUDE_DIRS PIXMAN_LIBRARIES)
