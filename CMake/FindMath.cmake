#.rst:
# FindMath
# -------
#
# Find standard C math library
#
# Try to find standard C math library. The following values are defined
#
# ::
#
#   MATH_FOUND       - True if math is available
#   MATH_LIBRARY     - Library for math
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

set_package_properties(Math PROPERTIES
   DESCRIPTION "Standard C math library")

find_library(MATH_LIBRARY m)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MATH DEFAULT_MSG MATH_LIBRARY)
mark_as_advanced(MATH_LIBRARY)
