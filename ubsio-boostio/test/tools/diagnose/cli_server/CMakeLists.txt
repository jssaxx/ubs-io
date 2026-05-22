aux_source_directory(. SERVER_DIAGNOSE_SRCS)
add_library(server_diagnose_shared SHARED ${SERVER_DIAGNOSE_SRCS})
set_target_properties(server_diagnose_shared PROPERTIES
        OUTPUT_NAME bioserver_diagnose
        SKIP_BUILD_RPATH TRUE
        INSTALL_RPATH ""
        BUILD_WITH_INSTALL_RPATH TRUE
)
target_include_directories(server_diagnose_shared PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(server_diagnose_shared PUBLIC bio_server)
target_include_directories(server_diagnose_shared PUBLIC ${CMAKE_SOURCE_DIR}/src/server)
