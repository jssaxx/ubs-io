file(GLOB_RECURSE BIO_SERVER_SRCS bio_server.cpp mirror_server.cpp mirror_server_crb.cpp)
add_library(bio_server SHARED ${BIO_SERVER_SRCS})
install(TARGETS bio_server DESTINATION ${TARGET_INSTALL_LIB_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

if (OPEN_CLI STREQUAL "ON")
    include_directories(../../test/tools/diagnose
            ../../test/tools/cli/include
            ../../test/tools/diagnose/cli_sdk
            ../../test/tools/diagnose/cli_htracer)
endif()

target_link_libraries(bio_server -Wl,--start-group rados -Wl,--end-group)

target_link_libraries(bio_server
        -Wl,--start-group
        pthread dl aio
        ${PROJECT_3RDPARTY_BIN_DIR}/hcom/lib/libhcom.so
        ${PROJECT_3RDPARTY_BIN_DIR}/bdm/lib/libbdm.so
        ${PROJECT_3RDPARTY_BIN_DIR}/zookeeper/lib/libzookeeper_mt.so
        ${PROJECT_3RDPARTY_BIN_DIR}/huawei_secure_c/lib/libsecurec.a
        htracer bio_common bio_flow bio_cache bio_net bio_conf bio_cluster bio_underfs
        -Wl,--end-group)
