cmake_minimum_required(VERSION 3.16)

# 设置项目路径
set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(SRC_DIR ${ROOT_DIR}/src)

# 链接库
link_directories(
        ${ROOT_DIR}/Build/src/disk
        ${ROOT_DIR}/Build/src/common
        ${ROOT_DIR}/Build/src/cluster
        ${ROOT_DIR}/Build/src/htracer
        ${ROOT_DIR}/dist/bio/lib
        ${ROOT_DIR}/dist/3rdparty/libboundscheck/lib
        ${ROOT_DIR}/dist/3rdparty/ubs-comm/lib
        /path/to/your/liburing/lib  # 替换为您自己的liburing库目录
        )

add_executable(bdm_benchmark bdm_benchmark.c)

# 包含头文件
target_include_directories(bdm_benchmark PRIVATE
        ${SRC_DIR}/disk/common
        ${SRC_DIR}/common
        ${SRC_DIR}/cluster
        ${SRC_DIR}/cluster/common
        ${SRC_DIR}/htracer
        /path/to/your/liburing/include  # 替换为您自己的liburing头文件目录
        )

target_link_libraries(bdm_benchmark
        -Wl,--start-group
        pthread dl aio
        bio_bdm bio_common bio_cluster htracer
        -lboundscheck
        -luring 
        -lstdc++
        -Wl,--end-group
        )
