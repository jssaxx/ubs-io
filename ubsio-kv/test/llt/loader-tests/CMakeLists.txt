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

add_executable(kv_loader_test
    loader_test.cpp
    ../unit-tests/main.cpp
)

target_include_directories(kv_loader_test PRIVATE
    ${GTEST_INSTALL_DIR}/include
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/csrc
    ${PROJECT_SOURCE_DIR}/src/csrc/kvc
    ${PROJECT_SOURCE_DIR}/src/csrc/nds
    ${PROJECT_SOURCE_DIR}/src/csrc/utils
    ${UBSIO_BOOSTIO_INCLUDE_DIR}
)

target_link_libraries(kv_loader_test PRIVATE
    ubsio_kvc
    ${GTEST_LIBRARY}
    pthread
    dl
)
add_dependencies(kv_loader_test ${KV_INCOMPLETE_STUB_TARGETS})

set_target_properties(kv_loader_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    BUILD_RPATH "\$ORIGIN/src/csrc"
)

add_test(NAME kv_loader_test COMMAND kv_loader_test)
set_tests_properties(kv_loader_test PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${KV_INCOMPLETE_STUB_DIR}:${CMAKE_BINARY_DIR}/src/csrc;ASCEND_HOME_PATH=${KV_INCOMPLETE_ASCEND_DIR}"
)
