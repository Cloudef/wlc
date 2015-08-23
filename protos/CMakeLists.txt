include(Wayland)

set(protos
   xdg-shell)

foreach(proto ${protos})
   add_feature_info(${proto} proto "Protocol extension")
   wayland_add_protocol_server(src "${proto}.xml" ${proto})
   list(APPEND sources ${src})
endforeach()

set_source_files_properties(${sources} PROPERTIES GENERATED ON)
add_library(wlc_protos STATIC ${sources})
