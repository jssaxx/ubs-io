file(GLOB_RECURSE MMS_CLIENT_SRCS *.cpp *.h)

add_library(mms_client SHARED ${MMS_CLIENT_SRCS})
install(TARGETS mms_client DESTINATION ${TARGET_INSTALL_LIB_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

if (OPEN_CLI STREQUAL "ON")
    include_directories(
            ${UBSIO_COMMON_CLI_DIR}/include/
            ${SRC_DIR}/../test/tools/diagnose/cli_client/
    )
endif()

target_link_libraries(mms_client -Wl,--start-group -Wl,--end-group)

target_link_libraries(mms_client
        -Wl,--start-group
        pthread dl expire_checker boundscheck_lib
        ${PROJECT_3RDPARTY_BIN_DIR}/ubs-comm/lib/libhcom.so
        tracer mms_common mms_memory mms_net mms_cache
        -Wl,--end-group)
