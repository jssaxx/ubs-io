file(GLOB_RECURSE BIO_PROMETHEUS *.cpp *.h)
add_library(bio_prometheus STATIC ${BIO_PROMETHEUS})
if (OPEN_CLI STREQUAL "ON")
    include_directories(
            ../bio_tp
            ../tracepoint/include
            ../cli/include
            ../diagnose/cli_sdk
    )
endif()

if(${CMAKE_BUILD_TYPE} STREQUAL "debug")
    target_link_libraries(bio_sdk
            -Wl,--start-group
            ${PROJECT_3RDPARTY_BIN_DIR}/prometheus/lib64/libprometheus-cpp-core.so
            ${PROJECT_3RDPARTY_BIN_DIR}/prometheus/lib64/libprometheus-cpp-pull.so
            ${PROJECT_3RDPARTY_BIN_DIR}/prometheus/lib64/libprometheus-cpp-push.so
            bio_prometheus
            -Wl,--end-group)
endif()