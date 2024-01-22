file(GLOB_RECURSE BIO_SERVER_SRCS *.cpp *.h)
add_library(bio_server STATIC ${BIO_SERVER_SRCS})

add_executable(bio_daemon bio_daemon.cpp bio_server.cpp mirror_server.cpp)
install(TARGETS bio_daemon DESTINATION ${TARGET_INSTALL_BIN_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

target_link_libraries(bio_daemon
        -Wl,--start-group
        pthread dl
        ${PROJECT_3RDPARTY_BIN_DIR}/hcom/lib/libhcom.so
		${PROJECT_3RDPARTY_BIN_DIR}/zookeeper/lib/libzookeeper_mt.so
        ${PROJECT_3RDPARTY_BIN_DIR}/huawei_secure_c/lib/libsecurec.a
		htracer bio_common aio bio_bdm bio_flow bio_cache bio_net bio_conf bio_cluster bio_net bio_underfs
        -Wl,--end-group)