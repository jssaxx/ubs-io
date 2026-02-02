set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${TARGET_INSTALL_LIB_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${TARGET_INSTALL_LIB_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${TARGET_INSTALL_BIN_DIR})
SET(BOOSTIO_TOOLS_INCLUDE_DIR
        ${PROJECT_SOURCE_DIR}/test/tools/tracepoint/include/
        ${PROJECT_SOURCE_DIR}/test/tools/bio_tp/
        PARENT_SCOPE)

link_directories(${TARGET_INSTALL_LIB_DIR})

if ("${OPEN_TP}" STREQUAL "ON")
    add_subdirectory(bio_tp)
    if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        add_subdirectory(tracepoint)
    endif()
endif ()

if ("${OPEN_PROMETHEUS}" STREQUAL "ON")
    add_subdirectory(prometheus)
endif()
