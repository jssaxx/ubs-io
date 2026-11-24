add_library(ascend_nds SHARED
    ascend_nds_dl.cpp
    ascend_nds_capi.cpp
)

target_include_directories(ascend_nds PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(ascend_nds PRIVATE pthread dl)
target_compile_options(ascend_nds PRIVATE -fstack-protector-strong -fno-omit-frame-pointer -ftrapv)
target_link_options(ascend_nds PRIVATE
    -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/ascend_nds.map"
)

set_target_properties(ascend_nds PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME ascend_nds
)

if (UBSIO_ENABLE_ORIGIN_RUNPATH)
    set_target_properties(ascend_nds PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

install(TARGETS ascend_nds
    LIBRARY DESTINATION ${OUTPUT_PATH}/lib
    ARCHIVE DESTINATION ${OUTPUT_PATH}/lib
    RUNTIME DESTINATION ${OUTPUT_PATH}/lib
)

install(FILES ${PROJECT_SOURCE_DIR}/include/ascend_nds.h
    DESTINATION ${OUTPUT_PATH}/include
)
