if (OPEN_CLI STREQUAL "ON" OR OPEN_TP STREQUAL "ON")
    add_subdirectory(benchmarks)
    add_subdirectory(tools)
endif()

if (${DEBUG_UT} MATCHES "ON")
    add_subdirectory(llt)
endif()

