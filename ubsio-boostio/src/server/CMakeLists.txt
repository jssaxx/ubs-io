file(GLOB_RECURSE BIO_SERVER_SRCS bio_server.cpp mirror_server.cpp mirror_server_crb.cpp standalone_view.cpp
        standalone_view.h standalone_memory_pool.cpp standalone_memory_pool.h)
add_library(bio_server SHARED ${BIO_SERVER_SRCS})
install(TARGETS bio_server DESTINATION ${TARGET_INSTALL_LIB_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

target_link_libraries(bio_server
        -Wl,--start-group
        pthread dl aio expire_checker
        hcom_lib
        boundscheck_lib
        htracer bio_common bio_bdm bio_flow bio_cache bio_net bio_conf bio_cluster
        -Wl,--end-group)
