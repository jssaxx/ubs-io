cmake_minimum_required(VERSION 3.13)
if(COMMAND cmake_policy)
    cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)
set(CMAKE_CXX_STANDARD 11)

aux_source_directory(cache MMS_TEST)
aux_source_directory(cluster MMS_TEST)
aux_source_directory(common MMS_TEST)
aux_source_directory(config MMS_TEST)
aux_source_directory(tracer MMS_TEST)
aux_source_directory(client MMS_TEST)
aux_source_directory(memory MMS_TEST)
aux_source_directory(net MMS_TEST)
aux_source_directory(server MMS_TEST)
add_executable(mms_test ${MMS_TEST} main.cpp ut_common.cpp ut_common.h)

set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(mms_test PRIVATE
        ${TEST_DIR}/mock
        ${TEST_DIR}/cache
        ${TEST_DIR}/cluster
        ${TEST_DIR}/common
        ${TEST_DIR}/config
        ${TEST_DIR}/tracer
        ${TEST_DIR}/client
        ${TEST_DIR}/memory
        ${TEST_DIR}/net
        ${SRC_DIR}/cache
        ${SRC_DIR}/cluster
        ${SRC_DIR}/common
        ${SRC_DIR}/config
        ${UBSIO_COMMON_TRACER_DIR}
        ${SRC_DIR}/client
        ${SRC_DIR}/memory
        ${SRC_DIR}/net
        ${SRC_DIR}/server)

target_link_libraries(mms_test GTest::gtest mockcpp mms_client mms_server
        -Wl,--start-group
        pthread dl rt
        -Wl,--end-group)

find_library(AIO_LIBRARY aio)
if (AIO_LIBRARY)
    target_link_libraries(mms_test ${AIO_LIBRARY})
endif()

set_target_properties(mms_test PROPERTIES OUTPUT_NAME "mms_test")
set_target_properties(mms_test PROPERTIES CLEAN_DIRECT_OUTPUT 1)
set_target_properties(mms_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test/llt")
