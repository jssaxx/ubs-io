add_subdirectory(libboundscheck)
add_subdirectory(ubs-comm)
add_subdirectory(libart)

if ("${OPEN_ZK}" STREQUAL "ON")
    add_subdirectory(zookeeper)
endif()
