#.rst:
# FindElogind
# -------
#
# Find Elogind library
#
# Try to find Elogind library on UNIX systems. The following values are defined
#
# ::
#
#   ELOGIND_FOUND         - True if Elogind is available
#   ELOGIND_INCLUDE_DIRS  - Include directories for Elogind
#   ELOGIND_LIBRARIES     - List of libraries for Elogind
#   ELOGIND_DEFINITIONS   - List of definitions for Elogind
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

include(FeatureSummary)
set_package_properties(Elogind PROPERTIES
   URL "https://github.com/elogind/elogind"
   DESCRIPTION "Elogind User, Seat and Session Manager")

find_package(PkgConfig)
pkg_check_modules(PC_ELOGIND QUIET libelogind)
find_library(ELOGIND_LIBRARIES NAMES elogind ${PC_ELOGIND_LIBRARY_DIRS})
find_path(ELOGIND_INCLUDE_DIRS elogind/sd-login.h HINTS ${PC_ELOGIND_INCLUDE_DIRS})

set(ELOGIND_DEFINITIONS ${PC_ELOGIND_CFLAGS_OTHER})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ELOGIND DEFAULT_MSG ELOGIND_INCLUDE_DIRS ELOGIND_LIBRARIES)
mark_as_advanced(ELOGIND_INCLUDE_DIRS ELOGIND_LIBRARIES ELOGIND_DEFINITIONS)
