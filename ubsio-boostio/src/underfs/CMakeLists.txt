add_library(bio_underfs SHARED ${BIO_UNDERFS_SRCS})
install(TARGETS bio_underfs DESTINATION ${TARGET_INSTALL_LIB_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
target_link_libraries(bio_underfs PRIVATE -Wl,--start-group rados -Wl,--end-group)
target_link_libraries(bio_underfs PRIVATE dl)