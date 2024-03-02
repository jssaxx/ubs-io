set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${TARGET_INSTALL_LIB_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${TARGET_INSTALL_LIB_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${TARGET_INSTALL_BIN_DIR})
SET(BOOSTIO_TOOLS_INCLUDE_DIR
        ${SOURCE_DIR}/test/tools/cli/include/
        ${SOURCE_DIR}/test/tools/tracepoint/include/
        ${SOURCE_DIR}/test/tools/diagnose/cli_htracer/
        ${SOURCE_DIR}/test/tools/diagnose/cli_sdk/
        PARENT_SCOPE)

link_directories(${TARGET_INSTALL_LIB_DIR})

add_subdirectory(cli)
if (CMAKE_BUILD_TYPE STREQUAL "debug"  AND CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    add_subdirectory(tracepoint)
endif()

add_subdirectory(diagnose)

