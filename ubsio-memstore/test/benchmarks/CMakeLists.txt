if (${DEBUG_UT} MATCHES "OFF")
	file(GLOB_RECURSE MMS_CONSOLE_SRCS *.cpp *.h)

	add_executable(mms_console ${MMS_CONSOLE_SRCS})
	install(TARGETS mms_console DESTINATION ${TARGET_INSTALL_BIN_DIR}/ PERMISSIONS OWNER_WRITE OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

	if (OPEN_CLI STREQUAL "ON")
		include_directories(
            ${UBSIO_COMMON_CLI_DIR}/include/
            ${SRC_DIR}/../test/tools/diagnose/cli_client/
            ${SRC_DIR}/../test/tools/diagnose/cli_server/
		)
	endif()

	target_link_libraries(mms_console
        -pie
        -Wl,--start-group
        pthread dl
        ${PROJECT_3RDPARTY_BIN_DIR}/ubs-comm/lib/libhcom.so
        mms_client
        -Wl,--end-group)
endif()
