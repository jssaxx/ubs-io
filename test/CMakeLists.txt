add_subdirectory(benchmarks)
if (OPEN_TEST_TOOLS STREQUAL "ON")
    add_subdirectory(tools)
endif()

if (${DEBUG_UT} MATCHES "ON")
    add_subdirectory(llt)
endif()