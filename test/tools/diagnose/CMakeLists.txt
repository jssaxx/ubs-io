include_directories(../cli/include
        ../tracepoint/include
        cli_sdk)


add_subdirectory(cli_sdk)
add_subdirectory(cli_server)

TARGET_INCLUDE_DIRECTORIES(bio_sdk PUBLIC ${BOOSTIO_TOOLS_INCLUDE_DIR})
TARGET_INCLUDE_DIRECTORIES(bio_server PUBLIC ${BOOSTIO_TOOLS_INCLUDE_DIR})

