cmake_minimum_required(VERSION 3.16)

# 设置项目路径
set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(SRC_DIR ${ROOT_DIR}/src)

# 链接库
link_directories(
        ${ROOT_DIR}/dist/bio/lib
        ${ROOT_DIR}/Build/src/disk
        )

add_executable(bdm_benchmark bdm_benchmark.c)

# 包含头文件
target_include_directories(bdm_benchmark PRIVATE
        ${SRC_DIR}/disk/common
        ${SRC_DIR}/common
        )

target_link_libraries(bdm_benchmark bio_bdm -luring -lstdc++)
