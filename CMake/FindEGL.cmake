# Find EGL
#
# EGL_INCLUDE_DIRS
# EGL_LIBRARIES
# EGL_DEFINITIONS
# EGL_FOUND

find_package(PkgConfig)
pkg_check_modules(PC_EGL QUIET egl)
find_library(EGL_LIBRARIES NAMES egl EGL HINTS ${PC_EGL_LIBRARY_DIRS})
find_path(EGL_INCLUDE_DIRS NAMES EGL/egl.h HINTS ${PC_EGL_INCLUDE_DIRS})

set(EGL_DEFINITIONS ${PC_EGL_CFLAGS_OTHER})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(EGL DEFAULT_MSG EGL_LIBRARIES EGL_INCLUDE_DIRS)
mark_as_advanced(EGL_INCLUDE_DIRS EGL_LIBRARIES)

