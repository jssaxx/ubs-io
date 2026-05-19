cmake_minimum_required(VERSION 3.16)
project(mmscore CXX C)

set(CMAKE_CXX_STANDARD 11)

message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

set(CMAKE_SKIP_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "")

SET(MMS_COMMON_FLAGS "")
if (CMAKE_BUILD_TYPE STREQUAL "debug")
    SET(HUAWEI_SECURE_BUILD_FLAGS "-fstack-protector-strong -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now -fPIE -fPIC")
else()
    SET(HUAWEI_SECURE_BUILD_FLAGS "-fstack-protector-strong -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now -fPIE -fPIC -s")
endif()
SET(C_COMMON_FLAGS "${C_COMMON_FLAGS} ${MMS_COMMON_FLAGS} -fno-omit-frame-pointer ${HUAWEI_SECURE_BUILD_FLAGS}")
SET(CXX_COMMON_FLAGS "${CXX_COMMON_FLAGS} ${MMS_COMMON_FLAGS} -fno-omit-frame-pointer ${HUAWEI_SECURE_BUILD_FLAGS}")

SET(CMAKE_C_FLAGS_DEBUG "${C_COMMON_FLAGS} -O0 -ggdb3 -g -lrt")
SET(CMAKE_C_FLAGS_RELEASE "${C_COMMON_FLAGS} -O3 -D_FORTIFY_SOURCE=2 -g -DNDEBUG=1 -lrt")

SET(CMAKE_CXX_FLAGS_DEBUG "${CXX_COMMON_FLAGS} -O0 -ggdb3 -g -lrt")
SET(CMAKE_CXX_FLAGS_RELEASE "${CXX_COMMON_FLAGS} -O3 -D_FORTIFY_SOURCE=2 -g -DNDEBUG=1 -lrt")

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(UBSIO_COMMON_DIR ${PROJECT_SOURCE_DIR}/../ubsio-common)
set(UBSIO_COMMON_TRACER_DIR ${UBSIO_COMMON_DIR}/tracer)
set(UBSIO_COMMON_CLI_DIR ${UBSIO_COMMON_DIR}/cli)
set(3RDPARTY_SRC_DIR ${PROJECT_SOURCE_DIR}/3rdparty)
set(OUTPUT_PATH ${PROJECT_SOURCE_DIR}/output)
set(BUILD_PATH ${PROJECT_BINARY_DIR}/)

set(PROJECT_3RDPARTY_SRC_DIR ${PROJECT_SOURCE_DIR}/3rdparty)
set(PROJECT_BUILD_PATH ${PROJECT_SOURCE_DIR}/build)
set(PROJECT_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/output)
set(PROJECT_3RDPARTY_BIN_DIR ${PROJECT_OUTPUT_PATH}/3rdparty)
set(ENV{3RDPARTY_BIN_DIR} ${PROJECT_3RDPARTY_BIN_DIR})

# set install target top dir
if (${CMAKE_INSTALL_PREFIX} MATCHES "/usr/local")
    set(TARGET_INSTALL_DIR ${OUTPUT_PATH}/mms)
elseif(NOT CMAKE_INSTALL_PREFIX)
    set(TARGET_INSTALL_DIR ${OUTPUT_PATH}/mms)
else()
    set(TARGET_INSTALL_DIR ${OUTPUT_PATH}/mms)
endif()

set(TARGET_INSTALL_BIN_DIR ${TARGET_INSTALL_DIR}/bin)
set(TARGET_INSTALL_INCLUDE_DIR ${TARGET_INSTALL_DIR}/include)
set(TARGET_INSTALL_LIB_DIR ${TARGET_INSTALL_DIR}/lib)

set(PROJECT_PATH_PREFIX "/etc/mms")

message(STATUS "3rdparty src dir ${PROJECT_3RDPARTY_SRC_DIR}")
message(STATUS "3rdparty bin dir ${PROJECT_3RDPARTY_BIN_DIR}")
message(STATUS "target install dir ${TARGET_INSTALL_DIR}")

include_directories(
        ${SRC_DIR}/cache
        ${SRC_DIR}/client
        ${SRC_DIR}/cluster
        ${SRC_DIR}/cluster/common
        ${SRC_DIR}/common
        ${SRC_DIR}/config
        ${SRC_DIR}/daemon
        ${UBSIO_COMMON_TRACER_DIR}
        ${SRC_DIR}/include
		${SRC_DIR}/memory
        ${SRC_DIR}/net
        ${SRC_DIR}/server
        ${SRC_DIR}/security/expiration_check
        ${SRC_DIR}/security/openssl_tools
        ${PROJECT_3RDPARTY_BIN_DIR}/libboundscheck/include
        ${PROJECT_3RDPARTY_BIN_DIR}/ubs-comm/include
        ${PROJECT_3RDPARTY_BIN_DIR}/ubs-comm/include/hcom
        ${PROJECT_3RDPARTY_BIN_DIR}/spdlog/include
		${PROJECT_3RDPARTY_BIN_DIR}/zookeeper/include
        ${PROJECT_3RDPARTY_BIN_DIR}/libart/src
        /usr/include/hcom
)

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    add_compile_options(-march=armv8-a+crc)
endif()

if (${DEBUG_UT} MATCHES "ON")
    set(PROJECT_PATH_PREFIX "../conf")
	add_compile_definitions(DEBUG_UT)
    add_compile_options(-ftest-coverage -fprofile-arcs)
    add_compile_options(-fno-inline)
    link_libraries(gcov)
endif()

add_definitions(-DPROJECT_PATH_PREFIX="${PROJECT_PATH_PREFIX}")

if (OPEN_CLI STREQUAL "ON")
    add_compile_definitions(USE_CLI_TOOLS)
endif()

if (OPEN_TP STREQUAL "ON")
    add_compile_definitions(USE_TP_TOOLS)
endif()

add_subdirectory(3rdparty)
add_subdirectory(conf)
add_subdirectory(src)
add_subdirectory(test)
