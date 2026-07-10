add_subdirectory(libboundscheck)
add_subdirectory(ubs-comm)
add_subdirectory(spdlog)
add_subdirectory(libaio)

if ("${OPEN_PROMETHEUS}" STREQUAL "ON")
    add_subdirectory(prometheus)
endif()
