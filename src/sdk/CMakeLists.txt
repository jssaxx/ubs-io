file(GLOB_RECURSE BIO_SDK_SRCS *.cpp *.h)

install(FILES bio.h DESTINATION ${TARGET_INSTALL_INCLUDE_DIR}/)

add_library(bio_sdk SHARED ${BIO_SDK_SRCS})
install(TARGETS bio_sdk DESTINATION ${TARGET_INSTALL_LIB_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

if (OPEN_TEST_TOOLS STREQUAL "ON")
    include_directories(
            ${SRC_DIR}/../test/tools/cli/include/
            ${SRC_DIR}/../test/tools/diagnose/cli_sdk/
            ${SRC_DIR}/../test/tools/diagnose/cli_htracer/
            ${SRC_DIR}/../test/tools/bio_tp/
            ${SRC_DIR}/../test/tools/tracepoint/include
    )
endif()

target_link_libraries(bio_sdk
        -Wl,--start-group
        pthread dl
        ${PROJECT_3RDPARTY_BIN_DIR}/hcom/lib/libhcom.so
        ${PROJECT_3RDPARTY_BIN_DIR}/zookeeper/lib/libzookeeper_mt.so
        ${PROJECT_3RDPARTY_BIN_DIR}/huawei_secure_c/lib/libsecurec.a
        htracer bio_common bio_net
        -Wl,--end-group)
