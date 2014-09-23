# Package finder for XCB libraries
#  - Be sure to set the COMPONENTS to the components you want to link to
#  - The XCB_LIBRARIES variable is set ONLY to your COMPONENTS list
#  - To use only a specific component
#	check the XCB_LIBRARIES_${COMPONENT} variable

# Define search directories depending on system
if(CMAKE_COMPILER_IS_GNUCC)
	set(_search_path_inc ENV CPATH)
	set(_search_path_lib ENV LIBRARY_PATH)
endif(CMAKE_COMPILER_IS_GNUCC)

find_path(XCB_INCLUDE_DIR xcb/xcb.h
	HINTS ${_search_path_inc})
find_library(XCB_MAIN_LIB xcb
	HINTS ${_search_path_lib})

set(XCB_LIBRARIES ${XCB_MAIN_LIB})
foreach(COMPONENT ${XCB_FIND_COMPONENTS})
	find_library(XCB_LIBRARIES_${COMPONENT} xcb-${COMPONENT}
		HINTS ${_search_path_lib})
	set(XCB_LIBRARIES ${XCB_LIBRARIES}
		${XCB_LIBRARIES_${COMPONENT}})
	mark_as_advanced(XCB_LIBRARIES_${COMPONENT})
endforeach(COMPONENT ${XCB_FIND_COMPONENTS})

mark_as_advanced(XCB_INCLUDE_DIR)
mark_as_advanced(XCB_MAIN_LIB)
set(XCB_LIBRARIES ${XCB_LIBRARIES} CACHE STRING
	"XCB_LIBRARIES" FORCE)

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(XCB DEFAULT_MSG
	XCB_LIBRARIES XCB_INCLUDE_DIR)

