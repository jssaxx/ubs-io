file(GLOB_RECURSE BIO_SDK_SRCS *.cpp *.h)

add_executable(bio_console ${BIO_SDK_SRCS})
install(TARGETS bio_console DESTINATION ${TARGET_INSTALL_BIN_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

target_link_libraries(bio_console
        -Wl,--start-group
        pthread dl
        ${PROJECT_3RDPARTY_BIN_DIR}/hcom/lib/libhcom.so
        ${PROJECT_3RDPARTY_BIN_DIR}/zookeeper/lib/libzookeeper_mt.so
        bio_sdk
        -Wl,--end-group)