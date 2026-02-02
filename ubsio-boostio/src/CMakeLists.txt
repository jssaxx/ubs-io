if ("${OPEN_TP}" STREQUAL "ON")
    include_directories(
            ${PROJECT_SOURCE_DIR}/test/tools/bio_tp/
            ${PROJECT_SOURCE_DIR}/test/tools/tracepoint/include
    )
endif()

add_subdirectory(cluster)
add_subdirectory(common)
add_subdirectory(config)
add_subdirectory(disk)
add_subdirectory(flow)
add_subdirectory(htracer)
add_subdirectory(net)
add_subdirectory(cache)
add_subdirectory(sdk)
add_subdirectory(server)
add_subdirectory(underfs)
add_subdirectory(io_interceptor)
add_subdirectory(interceptor)
add_subdirectory(daemon)
add_subdirectory(security)