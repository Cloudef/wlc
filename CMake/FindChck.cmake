#.rst:
# FindChck
# -------
#
# Find chck library
#
# Try to find chck library. The following values are defined
#
# ::
#
#   CHCK_FOUND         - True if chck is available
#   CHCK_INCLUDE_DIRS  - Include directories for chck
#   CHCK_LIBRARIES     - List of libraries for chck
#   CHCK_DEFINITIONS   - List of definitions for chck
#
#=============================================================================
# Copyright (c) 2015 Jari Vetoniemi
#
# Distributed under the OSI-approved BSD License (the "License");
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the License for more information.
#=============================================================================

unset(CHCK_INCLUDE_DIRS CACHE)
unset(CHCK_LIBRARIES CACHE)

include(FeatureSummary)
set_package_properties(chck PROPERTIES
   URL "https://github.com/Cloudef/chck/"
   DESCRIPTION "Collection of C utilities (Shared linking not recommended)")

find_package(PkgConfig)
pkg_check_modules(PC_CHCK QUIET chck)
find_path(CHCK_INCLUDE_DIRS NAMES chck/macros.h HINTS ${PC_CHCK_INCLUDE_DIRS})

set(libraries
   chck-atlas
   chck-buffer
   chck-dl
   chck-fs
   chck-lut
   chck-pool
   chck-sjis
   chck-string
   chck-tqueue
   chck-unicode
   chck-xdg)

unset(libs)
foreach(lib ${libraries})
   find_library(CHCK_LIBRARIES_${lib} NAMES ${lib} HINTS ${PC_CHCK_LIBRARY_DIRS})
   list(APPEND libs ${CHCK_LIBRARIES_${lib}})
   mark_as_advanced(CHCK_LIBRARIES_${lib})
   unset(CHCK_LIBRARIES_${lib} CACHE)
endforeach (lib ${libraries})

set(CHCK_LIBRARIES ${libs} CACHE FILEPATH "Path to chck libraries" FORCE)
set(CHCK_DEFINITIONS ${PC_CHCK_CFLAGS_OTHER})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(chck DEFAULT_MSG CHCK_LIBRARIES CHCK_INCLUDE_DIRS)
mark_as_advanced(CHCK_LIBRARIES CHCK_INCLUDE_DIRS CHCK_DEFINITIONS)
unset(libs)
