add_executable(mmsd mms_daemon.cpp)
install(TARGETS mmsd DESTINATION ${TARGET_INSTALL_BIN_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

if (CMAKE_BUILD_TYPE STREQUAL "debug" OR CMAKE_BUILD_TYPE STREQUAL "ASAN" OR CMAKE_BUILD_TYPE STREQUAL "TSAN")
	target_compile_definitions(mmsd PUBLIC _DEBUG)
endif()

target_link_libraries(mmsd
	-pie
	-Wl,--start-group
	pthread dl
	${PROJECT_3RDPARTY_BIN_DIR}/ubs-comm/lib/libhcom.so
	${PROJECT_3RDPARTY_BIN_DIR}/zookeeper/lib/libzookeeper_mt.so
	mms_server
	-Wl,--end-group)
