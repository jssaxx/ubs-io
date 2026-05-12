include(FetchContent)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://codehub.devcloud.cn-north-4.huaweicloud.com/aca5f619a7a34d3fb99b76a842fda236/googletest.git
        GIT_TAG        release-1.12.1
        GIT_SHALLOW    true
        GIT_PROGRESS   true
)
FetchContent_MakeAvailable(googletest)

set(MMS_MOCKCPP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mockcpp)
if (EXISTS ${MMS_MOCKCPP_DIR}/CMakeLists.txt)
    add_subdirectory(${MMS_MOCKCPP_DIR} ${CMAKE_BINARY_DIR}/mockcpp)
else()
    message(FATAL_ERROR "mockcpp CMakeLists.txt not found: ${MMS_MOCKCPP_DIR}")
endif()

set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/unit-tests)

include_directories(${SRC_DIR}/cache
        ${SRC_DIR}/mock
        ${SRC_DIR}/cluster
        ${SRC_DIR}/common
        ${SRC_DIR}/config
        ${SRC_DIR}/tracer
        ${SRC_DIR}/client
        ${SRC_DIR}/memory
        ${SRC_DIR}/net
        ${SRC_DIR}/server
        ${PROJECT_3RDPARTY_BIN_DIR}/ubs-comm/include/hcom
        ${PROJECT_3RDPARTY_BIN_DIR}/spdlog/include/spdlog
        ${PROJECT_3RDPARTY_BIN_DIR}/zookeeper/include/zookeeper
        ${TEST_DIR}/cluster
        ${TEST_DIR}/server)

link_directories(
        ${PROJECT_3RDPARTY_BIN_DIR}/ubs-comm/lib/
        ${PROJECT_3RDPARTY_BIN_DIR}/spdlog/lib64/
        ${PROJECT_3RDPARTY_BIN_DIR}/zookeeper/lib/
)

add_subdirectory(unit-tests)
