include_directories(${UBSIO_COMMON_CLI_DIR}/include
        cli_client
        cli_server)

add_subdirectory(cli_client)
add_subdirectory(cli_server)

if (OPEN_CLI STREQUAL "ON")
    target_link_libraries(mms_client cli_agent_shared)
    target_link_libraries(mms_server cli_agent_shared)
    TARGET_INCLUDE_DIRECTORIES(mms_client PUBLIC ${MMS_TOOLS_INCLUDE_DIR})
    TARGET_INCLUDE_DIRECTORIES(mms_server PUBLIC ${MMS_TOOLS_INCLUDE_DIR})
endif()
