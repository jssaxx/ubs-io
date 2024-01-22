file(GLOB_RECURSE BIO_SDK_SRCS *.cpp *.h)

add_library(bio_sdk STATIC ${BIO_SDK_SRCS})
install(FILES bio.h DESTINATION ${TARGET_INSTALL_INCLUDE_DIR}/)

add_executable(bio_console ${BIO_SDK_SRCS})
install(TARGETS bio_console DESTINATION ${TARGET_INSTALL_BIN_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

target_link_libraries(bio_console
        -Wl,--start-group
        pthread dl
        ${PROJECT_3RDPARTY_BIN_DIR}/hcom/lib/libhcom.so
        ${PROJECT_3RDPARTY_BIN_DIR}/zookeeper/lib/libzookeeper_mt.so
        ${PROJECT_3RDPARTY_BIN_DIR}/huawei_secure_c/lib/libsecurec.a
        htracer bio_common aio bio_bdm bio_flow bio_cache bio_net bio_conf bio_cluster bio_net bio_sdk bio_server bio_underfs
        -Wl,--end-group)
