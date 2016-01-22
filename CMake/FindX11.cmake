#.rst:
# FindX11
# -------
#
# Find X11 libraries
#
# Tries to find X11 libraries on unix systems.
#
# - Be sure to set the COMPONENTS to the components you want to link to
# - The X11_LIBRARIES variable is set ONLY to your COMPONENTS list
# - To use only a specific component check the X11_LIBRARIES_${COMPONENT} variable
#
# The following values are defined
#
# ::
#
#   X11_FOUND         - True if X11 is available
#   X11_INCLUDE_DIRS  - Include directories for X11
#   X11_LIBRARIES     - List of libraries for X11
#   X11_DEFINITIONS   - List of definitions for X11
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
set_package_properties(X11 PROPERTIES
   URL "http://www.x.org/"
   DESCRIPTION "Open source implementation of the X Window System")

find_package(PkgConfig)
pkg_check_modules(PC_X11 QUIET X11 ${X11_FIND_COMPONENTS})

find_library(X11_LIBRARIES X11 HINTS ${PC_X11_LIBRARY_DIRS})
find_path(X11_INCLUDE_DIRS X11/Xlib.h PATH_SUFFIXES X11 HINTS ${PC_X11_INCLUDE_DIRS})

foreach(COMPONENT ${X11_FIND_COMPONENTS})
	find_library(X11_LIBRARIES_${COMPONENT} ${COMPONENT} HINTS ${PC_X11_LIBRARY_DIRS})
	list(APPEND X11_LIBRARIES ${X11_LIBRARIES_${COMPONENT}})
	mark_as_advanced(X11_LIBRARIES_${COMPONENT})
endforeach(COMPONENT ${X11_FIND_COMPONENTS})

set(X11_DEFINITIONS ${PC_X11_CFLAGS_OTHER})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(X11 DEFAULT_MSG X11_LIBRARIES X11_INCLUDE_DIRS)
mark_as_advanced(X11_INCLUDE_DIRS X11_LIBRARIES X11_DEFINITIONS)
