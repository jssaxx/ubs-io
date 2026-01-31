# release 默认不编bio_console, 加上--cli, 编
if ("${OPEN_RELEASE}" STREQUAL "OFF" OR (OPEN_RELEASE STREQUAL "ON" AND OPEN_CLI STREQUAL "ON"))
    add_subdirectory(benchmarks)
endif()

if ("${OPEN_CLI}" STREQUAL "ON" OR OPEN_TP STREQUAL "ON" OR ${CMAKE_BUILD_TYPE} STREQUAL "debug")
    add_subdirectory(tools)
endif()

if (${DEBUG_UT} MATCHES "ON")
    add_subdirectory(llt)
endif()