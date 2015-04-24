# Find UDEV
#
# UDEV_INCLUDE_DIRS
# UDEV_LIBRARIES
# UDEV_DEFINITIONS
# UDEV_FOUND

find_package(PkgConfig)
pkg_check_modules(PC_UDEV QUIET libudev)
find_library(UDEV_LIBRARIES NAMES udev HINTS ${PC_UDEV_LIBRARY_DIRS})
find_path(UDEV_INCLUDE_DIRS libudev.h HINTS ${PC_UDEV_INCLUDE_DIRS})

set(UDEV_DEFINITIONS ${PC_UDEV_CFLAGS_OTHER})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(UDEV DEFAULT_MSG UDEV_INCLUDE_DIRS UDEV_LIBRARIES)
mark_as_advanced(UDEV_INCLUDE_DIRS UDEV_LIBRARIES)
