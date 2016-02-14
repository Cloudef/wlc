add_compile_options(-fvisibility=hidden)

set(CHCK_BUILD_STATIC ON CACHE STRING "Build chck statically if not found systemwide" FORCE)
set(CHCK_BUILD_TESTS OFF CACHE STRING "Do not build chck tests" FORCE)
add_subproject(chck Chck)
