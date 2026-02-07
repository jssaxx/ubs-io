aux_source_directory(. SDK_DIAGNOSE_SRCS)
add_library(sdk_diagnose_shared SHARED ${SDK_DIAGNOSE_SRCS})
set_target_properties(sdk_diagnose_shared PROPERTIES
        OUTPUT_NAME sdk_diagnose
        SKIP_BUILD_RPATH TRUE
        INSTALL_RPATH ""
        BUILD_WITH_INSTALL_RPATH TRUE
)
target_include_directories(sdk_diagnose_shared PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(sdk_diagnose_shared PUBLIC bio_sdk bio_conf)
target_include_directories(sdk_diagnose_shared PUBLIC ${CMAKE_SOURCE_DIR}/src/sdk)