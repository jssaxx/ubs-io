add_subdirectory(libboundscheck)
add_subdirectory(ubs-comm)
add_subdirectory(spdlog)

if ("${OPEN_ZK}" STREQUAL "ON")
    add_subdirectory(zookeeper)
endif()

if ("${OPEN_PROMETHEUS}" STREQUAL "ON")
    add_subdirectory(prometheus)
endif()
