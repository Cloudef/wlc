# subproject.cmake:
#
#    Builds another dependant CMake project, unless the project was already built or installed systemwide.
#
#    It does this by first checking if project exists systemwide and uses that.
#    If not, it checks if the project was already included and if it was, it does nothing.
#    If both of the above are not true, another CMake project is build.
#
# Usually the subprojects are git submodules on git repository, so packagers can avoid linking subprojects and
# instead always use systemwide libraries, by not downloading submodules. This will normally cause build to fail
# if systemwide package was not found.
#
# Developers can control this behaviour with -DSOURCE_<SUBPROJECT>=ON|OFF option. It's usually useful to have
# local development versions of everything when developing, and this option makes sure nothing is linked against
# systemwide libraries.
#
# If subprojects are being linked locally, and subprojects include subprojects that were already included.
# They will not include the subproject again. Instead they link against the already compiled subprojects.
#
# This is convenient for development, but not so covenient if you need to have different versions of submodules
# in-tree for some reason. Rather than doing that I just suggest updating the codebase to work with same library versions.

function(add_subproject name)
   if(ARGC GREATER 1)
      set(package_name ${ARGV1})
   else()
      set(package_name ${name})
   endif()

   if(TARGET ${name})
      message("Subproject ${name} already included, skipping")
   else()
      find_package(${package_name} QUIET)
      string(TOUPPER ${package_name} upper)
      if(NOT SOURCE_${upper} AND ${upper}_FOUND)
         message("Found ${package_name} on system")
      else()
         message("Adding ${name} as subdirectory")
         add_subdirectory(${name} EXCLUDE_FROM_ALL)
      endif()
   endif()
endfunction()
