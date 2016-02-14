include(Wayland)
find_package(WaylandProtocols)

if (WLPROTO_FOUND AND NOT SOURCE_WLPROTO)
   set(prefix "${WLPROTO_PATH}")
else ()
   set(prefix "wayland-protocols")
endif ()

include_directories(
   ${WAYLAND_SERVER_INCLUDE_DIRS}
)

set(protos
   "${prefix}/unstable/xdg-shell/xdg-shell-unstable-v5")

foreach(proto ${protos})
   add_feature_info(${proto} proto "Protocol extension")
   get_filename_component(base ${proto} NAME)
   wayland_add_protocol_server(src "${proto}.xml" ${base})
   list(APPEND sources ${src})
endforeach()

set_source_files_properties(${sources} PROPERTIES GENERATED ON)
add_library(wlc-protos STATIC ${sources})

set(test_protos
   test-extension)

foreach(proto ${test_protos})
   wayland_add_protocol_server(src "${proto}.xml" ${proto})
   wayland_add_protocol_client(src "${proto}.xml" ${proto})
   list(APPEND test_sources ${src})
endforeach()

set_source_files_properties(${test_sources} PROPERTIES GENERATED ON)
add_library(wlc-tests-protos STATIC ${test_sources})
