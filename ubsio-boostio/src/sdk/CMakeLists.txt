file(GLOB_RECURSE BIO_SDK_SRCS *.cpp *.h)

install(FILES bio_c.h DESTINATION ${TARGET_INSTALL_INCLUDE_DIR}/)

add_library(bio_sdk SHARED ${BIO_SDK_SRCS})

target_link_libraries(bio_sdk
        -Wl,--start-group
        pthread dl expire_checker rados
        ${PROJECT_3RDPARTY_BIN_DIR}/ubs-comm/lib/libhcom.so
        ${PROJECT_3RDPARTY_BIN_DIR}/zookeeper/lib/libzookeeper_mt.so
        ${PROJECT_3RDPARTY_BIN_DIR}/libboundscheck/lib/libboundscheck.a
        htracer bio_common bio_net bio_interceptor_server bio_underfs
        -Wl,--end-group)

set(LIB_VERSION_MAJOR 1)
set(LIB_VERSION_MINOR 0)
set(LIB_VERSION_PATCH 0)
set(LIB_VERSION "${LIB_VERSION_MAJOR}.${LIB_VERSION_MINOR}.${LIB_VERSION_PATCH}")
set(SO_VERSION "${LIB_VERSION_MAJOR}")

set_target_properties(bio_sdk PROPERTIES
        VERSION ${LIB_VERSION}
        SO_VERSION ${SO_VERSION}
)

install(TARGETS bio_sdk DESTINATION ${TARGET_INSTALL_LIB_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)