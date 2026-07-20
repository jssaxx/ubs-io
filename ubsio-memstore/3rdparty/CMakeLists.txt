add_subdirectory(libboundscheck)
add_subdirectory(ubs-comm)

if ("${OPEN_ZK}" STREQUAL "ON")
    add_subdirectory(zookeeper)
endif()
