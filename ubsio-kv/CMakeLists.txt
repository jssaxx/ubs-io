cmake_minimum_required(VERSION 3.16)

project(ubsio-kv VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)

message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

set(KV_COMMON_FLAGS "")
if (CMAKE_BUILD_TYPE STREQUAL "debug")
    set(HUAWEI_SECURE_BUILD_FLAGS "-fstack-protector-strong -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now -fPIE -fPIC -ftrapv")
else()
    set(HUAWEI_SECURE_BUILD_FLAGS "-fstack-protector-strong -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now -fPIE -fPIC -ftrapv -s")
endif()
set(C_COMMON_FLAGS "${C_COMMON_FLAGS} ${KV_COMMON_FLAGS} -fno-omit-frame-pointer ${HUAWEI_SECURE_BUILD_FLAGS}")
set(CXX_COMMON_FLAGS "${CXX_COMMON_FLAGS} ${KV_COMMON_FLAGS} -fno-omit-frame-pointer ${HUAWEI_SECURE_BUILD_FLAGS}")

set(CMAKE_C_FLAGS_DEBUG "${C_COMMON_FLAGS} -O0 -ggdb3 -g -lrt")
set(CMAKE_C_FLAGS_RELEASE "${C_COMMON_FLAGS} -O3 -D_FORTIFY_SOURCE=2 -g -DNDEBUG=1 -lrt")

set(CMAKE_CXX_FLAGS_DEBUG "${CXX_COMMON_FLAGS} -O0 -ggdb3 -g -lrt")
set(CMAKE_CXX_FLAGS_RELEASE "${CXX_COMMON_FLAGS} -O3 -D_FORTIFY_SOURCE=2 -g -DNDEBUG=1 -lrt")

set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(OUTPUT_PATH ${PROJECT_SOURCE_DIR}/dist)
set(BUILD_PATH ${PROJECT_BINARY_DIR}/)

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

add_subdirectory(src)
