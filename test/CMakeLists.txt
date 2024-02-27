add_subdirectory(benchmarks)
if (CMAKE_BUILD_TYPE STREQUAL "debug"  AND CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    add_subdirectory(tools)
endif()

if (${DEBUG_UT} MATCHES "ON")
    add_subdirectory(llt)
endif()