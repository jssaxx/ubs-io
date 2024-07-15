cmake_minimum_required(VERSION 3.16)
project(boostio CXX C)

set(CMAKE_CXX_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE release)
endif()
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

SET(BOOSTIO_COMMON_FLAGS "")
if (CMAKE_BUILD_TYPE STREQUAL "debug")
    SET(HUAWEI_SECURE_BUILD_FLAGS "-fstack-protector-strong -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now -fPIE -fPIC")
else()
    SET(HUAWEI_SECURE_BUILD_FLAGS "-fstack-protector-strong -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now -fPIE -fPIC -s")
endif()
SET(C_COMMON_FLAGS "${C_COMMON_FLAGS} ${BOOSTIO_COMMON_FLAGS} -fno-omit-frame-pointer ${HUAWEI_SECURE_BUILD_FLAGS}")
SET(CXX_COMMON_FLAGS "${CXX_COMMON_FLAGS} ${BOOSTIO_COMMON_FLAGS} -fno-omit-frame-pointer ${HUAWEI_SECURE_BUILD_FLAGS}")
 
SET(CMAKE_C_FLAGS_DEBUG "${C_COMMON_FLAGS} -O0 -ggdb3 -g -lrt")
SET(CMAKE_C_FLAGS_RELEASE "${C_COMMON_FLAGS} -O3 -D_FORTIFY_SOURCE=2 -g -DNDEBUG=1 -lrt")
 
SET(CMAKE_CXX_FLAGS_DEBUG "${CXX_COMMON_FLAGS} -O0 -ggdb3 -g -lrt")
SET(CMAKE_CXX_FLAGS_RELEASE "${CXX_COMMON_FLAGS} -O3 -D_FORTIFY_SOURCE=2 -g -DNDEBUG=1 -lrt")

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(3RDPARTY_SRC_DIR ${PROJECT_SOURCE_DIR}/3rdparty)
set(OUTPUT_PATH ${PROJECT_SOURCE_DIR}/output)
set(BUILD_PATH ${PROJECT_BINARY_DIR}/)

set(STUB_SRC_DIR ${PROJECT_SOURCE_DIR}/test/stub)

set(PROJECT_3RDPARTY_SRC_DIR ${PROJECT_SOURCE_DIR}/3rdparty)
set(PROJECT_BUILD_PATH ${PROJECT_SOURCE_DIR}/build)
set(PROJECT_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/output)
set(PROJECT_3RDPARTY_BIN_DIR ${PROJECT_OUTPUT_PATH}/3rdparty)
set(ENV{3RDPARTY_BIN_DIR} ${PROJECT_3RDPARTY_BIN_DIR})

# set install target top dir
if (${CMAKE_INSTALL_PREFIX} MATCHES "/usr/local")
    set(TARGET_INSTALL_DIR ${OUTPUT_PATH}/bio)
elseif(NOT CMAKE_INSTALL_PREFIX)
    set(TARGET_INSTALL_DIR ${OUTPUT_PATH}/bio)
else()
    set(TARGET_INSTALL_DIR ${OUTPUT_PATH}/bio)
endif()

set(TARGET_INSTALL_BIN_DIR ${TARGET_INSTALL_DIR}/bin)
set(TARGET_INSTALL_INCLUDE_DIR ${TARGET_INSTALL_DIR}/include)
set(TARGET_INSTALL_LIB_DIR ${TARGET_INSTALL_DIR}/lib)

message(STATUS "3rdparty src dir ${PROJECT_3RDPARTY_SRC_DIR}")
message(STATUS "3rdparty bin dir ${PROJECT_3RDPARTY_BIN_DIR}")
message(STATUS "target install dir ${TARGET_INSTALL_DIR}")

include_directories(
        ${SRC_DIR}/common
        ${SRC_DIR}/net
        ${SRC_DIR}/cache
        ${SRC_DIR}/cache/read
        ${SRC_DIR}/cache/write
        ${SRC_DIR}/cache/overloadctrl
        ${SRC_DIR}/cluster
		${SRC_DIR}/cluster/common
        ${SRC_DIR}/config
        ${SRC_DIR}/disk
		${SRC_DIR}/flow
		${SRC_DIR}/htracer
        ${SRC_DIR}/message
        ${SRC_DIR}/net
        ${SRC_DIR}/sdk
        ${SRC_DIR}/server
        ${SRC_DIR}/stream
        ${SRC_DIR}/underfs
        ${SRC_DIR}/io_interceptor
        ${SRC_DIR}/interceptor
        ${SRC_DIR}/interceptor/server
        ${PROJECT_3RDPARTY_BIN_DIR}/huawei_secure_c/include
        ${PROJECT_3RDPARTY_BIN_DIR}/hcom/include
        ${PROJECT_3RDPARTY_BIN_DIR}/spdlog/include
		${PROJECT_3RDPARTY_BIN_DIR}/zookeeper/include
        ${PROJECT_3RDPARTY_BIN_DIR}/hseceasy/hse/cryption/include/cryption
        ${PROJECT_3RDPARTY_BIN_DIR}/hseceasy/hse/utils
)
if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    add_compile_options(-march=armv8-a+crc)
endif()

if (${DEBUG_UT} MATCHES "ON")
    add_compile_options(-ftest-coverage -fprofile-arcs)
	add_compile_definitions(DEBUG_UT)
    link_libraries(gcov)
endif()

if (OPEN_TP STREQUAL "ON")
    add_compile_definitions(USE_DEBUG_TP_TOOLS)
endif()

if (OPEN_CLI STREQUAL "ON")
    add_compile_definitions(USE_CLI_TOOLS)
endif()

if (OPEN_UNDERFS_CEPH STREQUAL "ON")
    add_compile_definitions(_ceph_Integrate)
endif()

add_subdirectory(3rdparty)
add_subdirectory(src)
add_subdirectory(configs)
add_subdirectory(test)