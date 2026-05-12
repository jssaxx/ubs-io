if ("${OPEN_CLI}" STREQUAL "ON")
    add_subdirectory(benchmarks)
endif()

if ("${OPEN_TP}" STREQUAL "ON" OR "${OPEN_CLI}" STREQUAL "ON" OR ${CMAKE_BUILD_TYPE} STREQUAL "debug")
    add_subdirectory(tools)
endif()

if (${DEBUG_UT} MATCHES "ON")
    add_subdirectory(llt)
    include(${PROJECT_SOURCE_DIR}/test/tools/scripts/mockcpp.cmake)
endif()