add_executable(test_nds test_nds.cpp)
target_include_directories(test_nds PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${ACL_INCLUDE_DIR}
)
target_link_libraries(test_nds PRIVATE ascend_nds ${ACL_LIBRARY} pthread)

if (UBSIO_ENABLE_ORIGIN_RUNPATH)
    set_target_properties(test_nds PROPERTIES
        BUILD_RPATH "$ORIGIN/../lib"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
endif()

install(TARGETS test_nds
    RUNTIME DESTINATION ${OUTPUT_PATH}/bin
)
