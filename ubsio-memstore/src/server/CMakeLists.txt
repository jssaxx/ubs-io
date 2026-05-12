file(GLOB_RECURSE MMS_SERVER_SRCS *.cpp *.h)
add_library(mms_server SHARED ${MMS_SERVER_SRCS})
install(TARGETS mms_server DESTINATION ${TARGET_INSTALL_LIB_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

if (OPEN_CLI STREQUAL "ON")
    include_directories(
            ${UBSIO_COMMON_CLI_DIR}/include
            ../../test/tools/diagnose/cli_server)
endif()

target_link_libraries(mms_server -Wl,--start-group -Wl,--end-group)

target_link_libraries(mms_server
        -Wl,--start-group
        pthread dl expire_checker boundscheck_lib
        ${PROJECT_3RDPARTY_BIN_DIR}/ubs-comm/lib/libhcom.so
        ${PROJECT_3RDPARTY_BIN_DIR}/zookeeper/lib/libzookeeper_mt.so
        tracer mms_common mms_memory mms_net mms_conf mms_cluster mms_cache
        -Wl,--end-group)
