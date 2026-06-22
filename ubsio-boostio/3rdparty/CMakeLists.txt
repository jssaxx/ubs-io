add_subdirectory(libboundscheck)
add_subdirectory(ubs-comm)

if ("${OPEN_PROMETHEUS}" STREQUAL "ON")
    add_subdirectory(prometheus)
endif()
