include_directories(../cli/include
        ../tracepoint/include
        cli_sdk)


add_subdirectory(cli_sdk)
add_subdirectory(cli_server)

if (OPEN_CLI STREQUAL "ON")
    target_link_libraries(bio_server
            sdk_diagnose_shared)
    target_link_libraries(bio_sdk cli_agent_shared)
    TARGET_INCLUDE_DIRECTORIES(bio_sdk PUBLIC ${BOOSTIO_TOOLS_INCLUDE_DIR})
    TARGET_INCLUDE_DIRECTORIES(bio_server PUBLIC ${BOOSTIO_TOOLS_INCLUDE_DIR})
endif()

