file(GLOB_RECURSE BIO_INTERCEPTOR_CLIENT_SRCS *.cpp *.h)

add_library(ock_iofwd_proxy SHARED ${BIO_INTERCEPTOR_CLIENT_SRCS})
include_directories(../../io_interceptor/include)
install(TARGETS ock_iofwd_proxy DESTINATION ${TARGET_INSTALL_LIB_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

target_link_libraries(ock_iofwd_proxy
        -Wl,--start-group
        pthread dl aio
        hcom_lib
        bio_common bio_net htracer
        -Wl,--end-group)
if (OPEN_TP STREQUAL "ON")
    message("interceptor client success")
    target_link_libraries(ock_iofwd_proxy
            tracepoint_shared)
endif()
