file(GLOB_RECURSE BIO_SERVER_SRCS bio_server.cpp mirror_server.cpp mirror_server_crb.cpp)
add_library(bio_server SHARED ${BIO_SERVER_SRCS})
install(TARGETS bio_server DESTINATION ${TARGET_INSTALL_LIB_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

target_link_libraries(bio_server -Wl,--start-group rados -Wl,--end-group)

target_link_libraries(bio_server
        -Wl,--start-group
        pthread dl aio expire_checker
        ${PROJECT_3RDPARTY_BIN_DIR}/ubs-comm/lib/libhcom.so
        ${PROJECT_3RDPARTY_BIN_DIR}/zookeeper/lib/libzookeeper_mt.so
        ${PROJECT_3RDPARTY_BIN_DIR}/libboundscheck/lib/libboundscheck.a
        htracer bio_common bio_bdm bio_flow bio_cache bio_net bio_conf bio_cluster bio_underfs
        -Wl,--end-group)
