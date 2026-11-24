cmake_minimum_required(VERSION 3.16)

project(ubsio-nds VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(UBSIO_ENABLE_ORIGIN_RUNPATH "Use $ORIGIN RUNPATH for bundled runtime libraries" OFF)
option(UBSIO_BUILD_NDS_BENCH "Build the Ascend ACL NDS benchmark" OFF)

set(OUTPUT_PATH "${PROJECT_SOURCE_DIR}/dist")

include(cmake/config_xds.cmake)
add_subdirectory(src)

if (UBSIO_BUILD_NDS_BENCH)
    find_path(ACL_INCLUDE_DIR acl/acl.h
        PATHS ENV ASCEND_TOOLKIT_HOME /usr/local/Ascend/ascend-toolkit/latest /usr/local/ascend_toolkit
        PATH_SUFFIXES include
    )
    find_library(ACL_LIBRARY ascendcl
        PATHS ENV ASCEND_TOOLKIT_HOME /usr/local/Ascend/ascend-toolkit/latest /usr/local/ascend_toolkit
        PATH_SUFFIXES lib64 lib
    )
    if (NOT ACL_INCLUDE_DIR OR NOT ACL_LIBRARY)
        message(FATAL_ERROR "NDS benchmark requires Ascend ACL; set ASCEND_TOOLKIT_HOME")
    endif()
    add_subdirectory(benchmarks)
endif()
