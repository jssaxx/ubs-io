file(GLOB_RECURSE BIO_SDK_SRCS *.cpp *.h)

add_executable(bio_console ${BIO_SDK_SRCS})
install(TARGETS bio_console DESTINATION ${TARGET_INSTALL_BIN_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

if (OPEN_TEST_TOOLS STREQUAL "ON")
    include_directories(
            ${SRC_DIR}/../test/tools/cli/include/
            ${SRC_DIR}/../test/tools/diagnose/cli_sdk/
            ${SRC_DIR}/../test/tools/diagnose/cli_htracer/
    )
endif()

if (OPEN_UNDERFS_CEPH STREQUAL "ON")
target_link_libraries(bio_server -Wl,--start-group rados -Wl,--end-group)
endif()

target_link_libraries(bio_console
        -Wl,--start-group
        pthread dl aio
        ${PROJECT_3RDPARTY_BIN_DIR}/hcom/lib/libhcom.so
        ${PROJECT_3RDPARTY_BIN_DIR}/zookeeper/lib/libzookeeper_mt.so
        bio_sdk
        -Wl,--end-group)

