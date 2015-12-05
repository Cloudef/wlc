#.rst:
# FindWayland
# -----------
#
# Find Xwayland installation
#
# Try to find Xxayland. The following values are defined
#
# ::
#
#   XWAYLAND_FOUND        - True if Wayland is found
#   XWAYLAND_EXECUTABLE   - Path the the Xwayland executable 
#
# ============================================================================

include(FeatureSummary)
set_package_properties(Xwayland PROPERTIES
   URL "http://wayland.freedesktop.org/"
   DESCRIPTION "Protocol for implementing compositors")

find_program(XWAYLAND_EXECUTABLE NAMES Xwayland)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(XWAYLAND DEFAULT_MSG XWAYLAND_EXECUTABLE)
mark_as_advanced(XWAYLAND_EXECUTABLE)
