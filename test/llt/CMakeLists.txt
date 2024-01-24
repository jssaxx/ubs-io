aux_source_directory(cache/read BIO_TEST)
aux_source_directory(cache/write BIO_TEST)
aux_source_directory(net/rpc BIO_TEST)
add_executable(bio_test ${BIO_TEST})

set(TEST_DIR ${PROJECT_SOURCE_DIR}/test)

target_include_directories(bio_test PUBLIC
        ${SRC_DIR}/common
        ${SRC_DIR}/net
        ${SRC_DIR}/cache
        ${SRC_DIR}/cache/read
        ${SRC_DIR}/cache/write
        ${SRC_DIR}/cluster
        ${SRC_DIR}/config
        ${SRC_DIR}/disk
        ${SRC_DIR}/htracer
        ${SRC_DIR}/net
        ${SRC_DIR}/sdk
        ${SRC_DIR}/server
        ${SRC_DIR}/stream
        ${SRC_DIR}/underfs
        ${PROJECT_3RDPARTY_BIN_DIR}/mockcpp/include
        ${TEST_DIR}/mock
        )

if(EXISTS "${PROJECT_3RDPARTY_BIN_DIR}/gtest/lib64")
    set(GTEST_LIB_DIR ${PROJECT_3RDPARTY_BIN_DIR}/gtest/lib64)
else()
    set(GTEST_LIB_DIR ${PROJECT_3RDPARTY_BIN_DIR}/gtest/lib)
endif()

set(MOCKCPP_LIB ${PROJECT_3RDPARTY_BIN_DIR}/mockcpp/lib/libmockcpp.a)

target_link_libraries(bio_test bio_server
        -Wl,--start-group
        pthread dl rt ${GTEST_LIB_DIR}/libgtest.a ${GTEST_LIB_DIR}/libgtest_main.a aio
        ${MOCKCPP_LIB}
        -Wl,--end-group)

include_directories(${PROJECT_3RDPARTY_BIN_DIR}/gtest/include)
include(GoogleTest)


gtest_discover_tests(bio_test)

