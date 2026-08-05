aux_source_directory(. SERVER_DIAGNOSE_SRCS)
add_library(server_diagnose_shared SHARED ${SERVER_DIAGNOSE_SRCS})
set_target_properties(server_diagnose_shared PROPERTIES
        OUTPUT_NAME server_diagnose
)
if (UBSIO_ENABLE_ORIGIN_RUNPATH)
        set_target_properties(server_diagnose_shared PROPERTIES
                BUILD_RPATH "$ORIGIN"
                INSTALL_RPATH "$ORIGIN"
                BUILD_WITH_INSTALL_RPATH TRUE
        )
        target_link_options(server_diagnose_shared PRIVATE "LINKER:--enable-new-dtags")
else()
        set_target_properties(server_diagnose_shared PROPERTIES
                SKIP_BUILD_RPATH TRUE
                INSTALL_RPATH ""
                BUILD_WITH_INSTALL_RPATH TRUE
        )
endif()
target_include_directories(server_diagnose_shared PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(server_diagnose_shared PUBLIC bio_server)
target_include_directories(server_diagnose_shared PUBLIC ${CMAKE_SOURCE_DIR}/src/server)
