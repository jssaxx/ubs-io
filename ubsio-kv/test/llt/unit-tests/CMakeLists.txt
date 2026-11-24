set(GTEST_INSTALL_DIR "${TEST_TOOL_INSTALL_PATH}/googletest")
if (NOT EXISTS "${GTEST_INSTALL_DIR}")
    message(FATAL_ERROR "GTEST_INSTALL_DIR(${GTEST_INSTALL_DIR}) is invalid")
endif()

find_library(GTEST_LIBRARY
    NAMES gtest
    PATHS "${GTEST_INSTALL_DIR}/lib64" "${GTEST_INSTALL_DIR}/lib"
    NO_DEFAULT_PATH
)
if (NOT GTEST_LIBRARY)
    message(FATAL_ERROR "libgtest was not found under ${GTEST_INSTALL_DIR}")
endif()

file(GLOB KV_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/kvc/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/nds/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/utils/*.cpp"
)
list(REMOVE_ITEM KV_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/nds_lifecycle_main.cpp")
add_executable(kv_test ${KV_TEST_SOURCES})

add_executable(kv_nds_lifecycle_test nds_lifecycle_main.cpp)
target_include_directories(kv_nds_lifecycle_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/csrc
    ${PROJECT_SOURCE_DIR}/src/csrc/kvc
    ${PROJECT_SOURCE_DIR}/src/csrc/utils
    ${PROJECT_SOURCE_DIR}/test/llt/stubs/common
    ${UBSIO_BOOSTIO_INCLUDE_DIR}
)
target_link_libraries(kv_nds_lifecycle_test PRIVATE ubsio_kvc ${KV_STUB_TARGETS} pthread dl)
add_dependencies(kv_nds_lifecycle_test ${KV_STUB_TARGETS} kv_nds_facade)
set_target_properties(kv_nds_lifecycle_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    BUILD_RPATH "\$ORIGIN/src/csrc"
)
add_test(NAME kv_nds_lifecycle_test COMMAND kv_nds_lifecycle_test)
set_tests_properties(kv_nds_lifecycle_test PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}:${KV_FAKE_ASCEND_DIR}/lib64;ASCEND_HOME_PATH=${KV_FAKE_ASCEND_DIR}"
)

target_include_directories(kv_test PRIVATE
    ${GTEST_INSTALL_DIR}/include
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/csrc
    ${PROJECT_SOURCE_DIR}/src/csrc/kvc
    ${PROJECT_SOURCE_DIR}/src/csrc/nds
    ${PROJECT_SOURCE_DIR}/src/csrc/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/kvc
    ${CMAKE_CURRENT_SOURCE_DIR}/nds
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
    ${PROJECT_SOURCE_DIR}/test/llt/stubs/common
    ${UBSIO_BOOSTIO_INCLUDE_DIR}
    ${UBSIO_ASCEND_NDS_INCLUDE_DIR}
)

target_link_libraries(kv_test PRIVATE
    ubsio_kvc
    ${KV_STUB_TARGETS}
    ${GTEST_LIBRARY}
    pthread
    dl
)
add_dependencies(kv_test kv_nds_facade)

set_target_properties(kv_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    BUILD_RPATH "\$ORIGIN;${KV_FAKE_ASCEND_DIR}/lib64"
)

add_test(NAME kv_test COMMAND kv_test)
set_tests_properties(kv_test PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}:${KV_FAKE_ASCEND_DIR}/lib64;ASCEND_HOME_PATH=${KV_FAKE_ASCEND_DIR}"
)
