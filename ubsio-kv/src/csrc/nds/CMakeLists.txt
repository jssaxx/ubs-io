
file(GLOB NDS_OBJ *.cpp *.h)

add_library(nds_object OBJECT ${NDS_OBJ})

target_include_directories(nds_object PUBLIC
    ${TARGET_INSTALL_LIB}/3rdparty/nds/include
    PRIVATE
    ${SRC_DIR}/ubsio-kv/src/csrc/utils
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(ubsio_nds SHARED $<TARGET_OBJECTS:nds_object>)

target_link_directories(ubsio_nds PRIVATE
    ${TARGET_INSTALL_LIB}/3rdparty/nds/lib
)

target_link_libraries(ubsio_nds PRIVATE
    -Wl,--start-group
    nds_file
    -Wl,--end-group
)

install(TARGETS dfc_nds DESTINATION ${TARGET_INSTALL_LIB}/dfc
    PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)