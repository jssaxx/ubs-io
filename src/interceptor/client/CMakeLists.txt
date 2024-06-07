file(GLOB_RECURSE BIO_INTERCEPTOR_CLIENT_SRCS *.cpp *.h)

add_library(ock_iofwd_proxy SHARED ${BIO_INTERCEPTOR_CLIENT_SRCS})
install(TARGETS ock_iofwd_proxy DESTINATION ${TARGET_INSTALL_LIB_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

target_link_libraries(ock_iofwd_proxy
        -Wl,--start-group
        pthread dl aio
        ${PROJECT_3RDPARTY_BIN_DIR}/hcom/lib/libhcom.so
        bio_common bio_net htracer
        -Wl,--end-group)
if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    target_link_libraries(ock_iofwd_proxy
            tracepoint_shared)
endif()