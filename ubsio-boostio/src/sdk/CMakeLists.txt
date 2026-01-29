file(GLOB_RECURSE BIO_SDK_SRCS *.cpp *.h)

install(FILES bio_c.h DESTINATION ${TARGET_INSTALL_INCLUDE_DIR}/)

add_library(bio_sdk SHARED ${BIO_SDK_SRCS})
install(TARGETS bio_sdk DESTINATION ${TARGET_INSTALL_LIB_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

include_directories(
        ${SRC_DIR}/../test/tools/cli/include/
        ${SRC_DIR}/../test/tools/diagnose/cli_sdk/
)

target_link_libraries(bio_sdk -Wl,--start-group rados -Wl,--end-group)

target_link_libraries(bio_sdk
        -Wl,--start-group
        pthread dl expire_checker
        ${PROJECT_3RDPARTY_BIN_DIR}/ubs-comm/lib/libhcom.so
        ${PROJECT_3RDPARTY_BIN_DIR}/zookeeper/lib/libzookeeper_mt.so
        ${PROJECT_3RDPARTY_BIN_DIR}/libboundscheck/lib/libsecurec.a
        htracer bio_common bio_net bio_interceptor_server bio_underfs
        -Wl,--end-group)