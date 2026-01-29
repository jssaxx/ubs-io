if (${DEBUG_UT} MATCHES "OFF")
	add_executable(bio_daemon bio_daemon.cpp)
	install(TARGETS bio_daemon DESTINATION ${TARGET_INSTALL_BIN_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

	if (CMAKE_BUILD_TYPE STREQUAL "debug" OR CMAKE_BUILD_TYPE STREQUAL "ASAN" OR CMAKE_BUILD_TYPE STREQUAL "TSAN")
		target_compile_definitions(bio_daemon PUBLIC _DEBUG)
	endif()

	target_link_libraries(bio_daemon
	    -pie
        -Wl,--start-group
        pthread dl
        ${PROJECT_3RDPARTY_BIN_DIR}/hcom/lib/libhcom.so
		${PROJECT_3RDPARTY_BIN_DIR}/zookeeper/lib/libzookeeper_mt.so
		bio_server
        -Wl,--end-group)
endif()