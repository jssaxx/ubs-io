file(GLOB_RECURSE BIO_INTERCEPTOR_SERVER_SRCS *.cpp *.h)
add_library(bio_interceptor_server SHARED ${BIO_INTERCEPTOR_SERVER_SRCS})
install(TARGETS bio_interceptor_server DESTINATION ${TARGET_INSTALL_LIB_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

if (OPEN_TEST_TOOLS STREQUAL "ON")
    include_directories(../../../test/tools/diagnose
            ../../../test/tools/cli/include
            ../../../test/tools/diagnose/cli_sdk
            ../../../test/tools/diagnose/cli_htracer)
endif()

target_link_libraries(bio_interceptor_server
        -Wl,--start-group
        pthread dl aio
        -Wl,--end-group)