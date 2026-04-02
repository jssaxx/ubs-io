#根据模块划分决定是否继续添add_subdirectory加子模块
file(GLOB_RECURSE KV_CLIENT_SRC *.cc *.cpp *.c)

add_library(kv_client_src_obj OBJECT ${KV_CLIENT_SRC})
add_dependencies(kv_client_src_obj GeneratedFlatBuffers)

add_library(dfc_client SHARED $<TARGET_OBJECTS:kv_client_src_obj>)

target_link_directories(dfc_client PRIVATE
        ${TARGET_INSTALL_LIB}/bio
        ${TARGET_INSTALL_LIB}/hcom
        ${TARGET_INSTALL_LIB}/zookeeper
        ${TARGET_INSTALL_LIB}/nds
)

target_link_libraries(dfc_client
    -Wl,--start-group
    pthread
    common
    net_engine
    kv_server
    dfc_nds
    -Wl,--end-group
)

target_include_directories(kv_client_src_obj PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${TARGET_INSTALL_LIB}/3rdparty/nds/include
    ${SRC_DIR}/dfc/csrc/common/hcom
    ${SRC_DIR}/dfc/csrc/common/message
    ${SRC_DIR}/dfc/csrc/common/resource_pool
    ${SRC_DIR}/dfc/csrc/common/utils
    ${SRC_DIR}/dfc/csrc/client/kv/nds
    ${SRC_DIR}/dfc/csrc/client/kv/net_engine
    ${SRC_DIR}/dfc/csrc/client/kv/kv_server
    ${SRC_DIR}/dfc/csrc/meta_store/meta_engine/meta_client
    ${SRC_DIR}/dfc/csrc/meta_store/remote_connection_def
    ${SRC_DIR}/dfc/csrc/meta_store/meta_engine/meta_net_engine
)

install(TARGETS dfc_client DESTINATION ${TARGET_INSTALL_LIB}/dfc PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
