# --- 路径设置 ---
set(SOURCE_DIR ${PROJECT_3RDPARTY_SRC_DIR}/libboundscheck/libboundscheck)
set(LIBBOUNDCHECK_INSTALL_DIR "${PROJECT_3RDPARTY_BIN_DIR}/libboundscheck")

message(STATUS "LIBBOUNDCHECK_INSTALL_DIR: ${LIBBOUNDCHECK_INSTALL_DIR}")

find_path(LIBBOUNDCHECK_FINAL_INCLUDE
        NAMES securec.h
        PATHS /usr/include ${LIBBOUNDCHECK_INSTALL_DIR}/include
        NO_DEFAULT_PATH
        )

find_library(LIBBOUNDCHECK_FINAL_LIBRARY
        NAMES libboundscheck.so boundscheck
        PATHS /usr/lib64 /usr/lib ${LIBBOUNDCHECK_INSTALL_DIR}/lib
        NO_DEFAULT_PATH
        )

if(NOT LIBBOUNDCHECK_FINAL_INCLUDE OR NOT LIBBOUNDCHECK_FINAL_LIBRARY)
    message(STATUS "libboundscheck not found! Attempting to build from source...")

    include(FetchContent)
    FetchContent_Declare(
            libboundscheck_src
            GIT_REPOSITORY https://gitcode.com/openeuler/libboundscheck.git
            GIT_TAG        v1.1.16
            SOURCE_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/libboundscheck"
    )
    FetchContent_MakeAvailable(libboundscheck_src)

    execute_process(
            COMMAND make CC=gcc -j8
            WORKING_DIRECTORY ${SOURCE_DIR}
            RESULT_VARIABLE MAKE_RESULT
    )

    if(NOT MAKE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to build libboundscheck from source!")
    endif()

    file(MAKE_DIRECTORY ${LIBBOUNDCHECK_INSTALL_DIR}/lib)
    file(MAKE_DIRECTORY ${LIBBOUNDCHECK_INSTALL_DIR}/include)

    file(COPY ${SOURCE_DIR}/lib/ DESTINATION ${LIBBOUNDCHECK_INSTALL_DIR}/lib)
    file(COPY ${SOURCE_DIR}/include/ DESTINATION ${LIBBOUNDCHECK_INSTALL_DIR}/include)

    set(LIBBOUNDCHECK_FINAL_INCLUDE "${LIBBOUNDCHECK_INSTALL_DIR}/include")
    set(LIBBOUNDCHECK_FINAL_LIBRARY "${LIBBOUNDCHECK_INSTALL_DIR}/lib/libboundscheck.so")
endif()

if(NOT TARGET boundscheck_lib)
    get_filename_component(BC_LIB_DIR "${LIBBOUNDCHECK_FINAL_LIBRARY}" DIRECTORY)

    add_library(boundscheck_lib INTERFACE IMPORTED GLOBAL)

    set_target_properties(boundscheck_lib PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBBOUNDCHECK_FINAL_INCLUDE}"
            INTERFACE_LINK_LIBRARIES "-L${BC_LIB_DIR} -lboundscheck"
            )

    set(LIBBOUNDCHECK_LIBRARIES boundscheck_lib CACHE INTERNAL "libboundscheck libraries")
    set(LIBBOUNDCHECK_INCLUDE_DIRS "${LIBBOUNDCHECK_FINAL_INCLUDE}" CACHE INTERNAL "libboundscheck includes")
endif()

message(STATUS "libboundscheck Target defined: ${LIBBOUNDCHECK_FINAL_LIBRARY}")