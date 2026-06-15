cmake_minimum_required(VERSION 3.16)
project(boostio CXX C)

set(CMAKE_CXX_STANDARD 11)

message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

option(BOOSTIO_ENABLE_ASAN_UBSAN "Build BoostIO targets with ASan and UBSan" OFF)

set(BOOSTIO_COMMON_FLAGS "")
set(BOOSTIO_TRAPV_FLAG "-ftrapv")
set(BOOSTIO_STRIP_FLAG "")
if (BOOSTIO_ENABLE_ASAN_UBSAN)
    if (NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "BOOSTIO_ENABLE_ASAN_UBSAN requires GCC or Clang.")
    endif()
    set(BOOSTIO_TRAPV_FLAG "")
    set(BOOSTIO_SANITIZER_COMPILE_OPTIONS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    set(BOOSTIO_SANITIZER_LINK_OPTIONS -fsanitize=address,undefined)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "debug")
    set(HUAWEI_SECURE_BUILD_FLAGS "-fstack-protector-strong -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now -fPIE -fPIC ${BOOSTIO_TRAPV_FLAG}")
else()
    if (NOT BOOSTIO_ENABLE_ASAN_UBSAN)
        set(BOOSTIO_STRIP_FLAG "-s")
    endif()
    set(HUAWEI_SECURE_BUILD_FLAGS "-fstack-protector-strong -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now -fPIE -fPIC ${BOOSTIO_TRAPV_FLAG} ${BOOSTIO_STRIP_FLAG}")
endif()
set(C_COMMON_FLAGS "${C_COMMON_FLAGS} ${BOOSTIO_COMMON_FLAGS} -fno-omit-frame-pointer ${HUAWEI_SECURE_BUILD_FLAGS}")
set(CXX_COMMON_FLAGS "${CXX_COMMON_FLAGS} ${BOOSTIO_COMMON_FLAGS} -fno-omit-frame-pointer ${HUAWEI_SECURE_BUILD_FLAGS}")

set(CMAKE_C_FLAGS_DEBUG "${C_COMMON_FLAGS} -O0 -ggdb3 -g -lrt")
set(CMAKE_C_FLAGS_RELEASE "${C_COMMON_FLAGS} -O3 -D_FORTIFY_SOURCE=2 -g -DNDEBUG=1 -lrt")

set(CMAKE_CXX_FLAGS_DEBUG "${CXX_COMMON_FLAGS} -O0 -ggdb3 -g -lrt")
set(CMAKE_CXX_FLAGS_RELEASE "${CXX_COMMON_FLAGS} -O3 -D_FORTIFY_SOURCE=2 -g -DNDEBUG=1 -lrt")

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(3RDPARTY_SRC_DIR  ${PROJECT_SOURCE_DIR}/3rdparty)
set(OUTPUT_PATH  ${PROJECT_SOURCE_DIR}/dist)
set(BUILD_PATH ${PROJECT_BINARY_DIR}/)

set(STUB_SRC_DIR  ${PROJECT_SOURCE_DIR}/test/stub)

set(PROJECT_3RDPARTY_SRC_DIR  ${PROJECT_SOURCE_DIR}/3rdparty)
set(PROJECT_BUILD_PATH ${PROJECT_SOURCE_DIR}/Build)
set(PROJECT_OUTPUT_PATH  ${PROJECT_SOURCE_DIR}/dist)
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
        ${SRC_DIR}/disk/common
	    ${SRC_DIR}/flow
	    ${SRC_DIR}/htracer
        ${SRC_DIR}/message
        ${SRC_DIR}/net
        ${SRC_DIR}/sdk
        ${SRC_DIR}/server
        ${SRC_DIR}/underfs
        ${SRC_DIR}/io_interceptor
        ${SRC_DIR}/interceptor
        ${SRC_DIR}/interceptor/server
        ${SRC_DIR}/security/expiration_check
        ${SRC_DIR}/security/openssl_tools
        ${3RDPARTY_SRC_DIR}/zookeeper
        ${3RDPARTY_SRC_DIR}/hadoop
        ${PROJECT_3RDPARTY_BIN_DIR}/libboundscheck/include
        ${PROJECT_3RDPARTY_BIN_DIR}/ubs-comm/include
        ${PROJECT_3RDPARTY_BIN_DIR}/spdlog/include
)

if ("${OPEN_PROMETHEUS}" STREQUAL "ON")
    include_directories(
            ${PROJECT_SOURCE_DIR}/test/tools/prometheus
            ${PROJECT_3RDPARTY_BIN_DIR}/prometheus/include
    )
    add_compile_definitions(USE_PROMETHEUS)
endif()

if(${CMAKE_BUILD_TYPE} STREQUAL "release")
    add_compile_definitions(OPEN_RELEASE)
endif()

if (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64")
    add_compile_options(-march=armv8-a+crc)
endif()

if (${DEBUG_UT} MATCHES "ON")
    enable_testing()
    add_compile_options(-ftest-coverage -fprofile-arcs)
	add_compile_definitions(DEBUG_UT)
    link_libraries(gcov)
    add_compile_options(-fno-inline)
endif()

if ("${OPEN_TP}" STREQUAL "ON")
    add_compile_definitions(USE_DEBUG_TP_TOOLS)
    include_directories(
            ${PROJECT_SOURCE_DIR}/test/tools/bio_tp/
            ${PROJECT_SOURCE_DIR}/test/tools/tracepoint/include
    )
endif()

add_subdirectory(3rdparty)
if (BOOSTIO_ENABLE_ASAN_UBSAN)
    message(STATUS "Enable ASan and UBSan for BoostIO targets.")
    add_compile_options(${BOOSTIO_SANITIZER_COMPILE_OPTIONS})
    add_link_options(${BOOSTIO_SANITIZER_LINK_OPTIONS})
    add_compile_definitions(BOOSTIO_ENABLE_ASAN_UBSAN)
endif()
add_subdirectory(src)
add_subdirectory(configs)
add_subdirectory(test)
