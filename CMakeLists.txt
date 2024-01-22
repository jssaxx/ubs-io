cmake_minimum_required(VERSION 3.16)
project(boostio CXX C)

set(CMAKE_CXX_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE release)
endif()
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

if (${CMAKE_BUILD_TYPE} MATCHES "release")
    add_compile_options(-O2)
else ()
    add_compile_options(-g -rdynamic)
endif (${CMAKE_BUILD_TYPE} MATCHES "release")
add_compile_options(-fPIC)
add_compile_options(-Wall -fms-extensions)
add_compile_options(-Wno-unused-parameter -Wno-unused-function -Wunused-variable -Wunused-value -Wcast-align)
add_compile_options(-Wcast-qual -Winvalid-pch -Wwrite-strings -Wsign-compare -Wfloat-equal -Wextra)

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
        ${PROJECT_3RDPARTY_BIN_DIR}/huawei_secure_c/include
        ${PROJECT_3RDPARTY_BIN_DIR}/hcom/include
        ${PROJECT_3RDPARTY_BIN_DIR}/spdlog/include
		${PROJECT_3RDPARTY_BIN_DIR}/zookeeper/include
)

if (${CMAKE_BUILD_TYPE} MATCHES "debug")
    add_compile_options(-ftest-coverage -fprofile-arcs)
    link_libraries(gcov)
endif()

add_subdirectory(3rdparty)
add_subdirectory(src)
add_subdirectory(configs)
add_subdirectory(test)