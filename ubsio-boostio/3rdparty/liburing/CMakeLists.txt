set(LIBURING_VERSION "2.6")
set(LIBURING_SOURCE_DIR "${PROJECT_3RDPARTY_SRC_DIR}/liburing/liburing")
set(LIBURING_INSTALL_DIR "${PROJECT_3RDPARTY_BIN_DIR}/liburing")
set(LIBURING_INCLUDE_DIR "${LIBURING_INSTALL_DIR}/include")
set(LIBURING_LIBRARY "${LIBURING_INSTALL_DIR}/lib/liburing.so.${LIBURING_VERSION}")

message(STATUS "LIBURING_INSTALL_DIR: ${LIBURING_INSTALL_DIR}")

find_path(LIBURING_SYSTEM_INCLUDE_DIR
    NAMES liburing.h
    PATHS /usr/include /usr/local/include
    NO_DEFAULT_PATH
)

find_library(LIBURING_SYSTEM_LIBRARY
    NAMES uring liburing.so
    PATHS /usr/lib64 /usr/lib /usr/local/lib64 /usr/local/lib /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}
    NO_DEFAULT_PATH
)

if(LIBURING_SYSTEM_INCLUDE_DIR AND LIBURING_SYSTEM_LIBRARY)
    message(STATUS "Found system liburing: ${LIBURING_SYSTEM_LIBRARY}")
    set(LIBURING_FINAL_INCLUDE_DIR "${LIBURING_SYSTEM_INCLUDE_DIR}")
    set(LIBURING_FINAL_LIBRARY "${LIBURING_SYSTEM_LIBRARY}")
else()
    if(LIBURING_SYSTEM_INCLUDE_DIR)
        message(STATUS "liburing header found but library missing. Building from source.")
    elseif(LIBURING_SYSTEM_LIBRARY)
        message(STATUS "liburing library found but header missing. Building from source.")
    else()
        message(STATUS "System liburing not found. Building from source.")
    endif()

    if(NOT EXISTS "${LIBURING_SOURCE_DIR}/configure")
        include(FetchContent)
        FetchContent_Declare(
            liburing_src
            GIT_REPOSITORY https://gitcode.com/gh_mirrors/li/liburing.git
            GIT_TAG        liburing-${LIBURING_VERSION}
            SOURCE_DIR     ${LIBURING_SOURCE_DIR}
        )
        FetchContent_GetProperties(liburing_src)
        if(NOT liburing_src_POPULATED)
            FetchContent_Populate(liburing_src)
        endif()
    endif()

    if(NOT EXISTS "${LIBURING_LIBRARY}" OR NOT EXISTS "${LIBURING_INCLUDE_DIR}/liburing.h")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env "CC=${CMAKE_C_COMPILER}"
                ./configure
                --prefix=${LIBURING_INSTALL_DIR}
                --includedir=${LIBURING_INCLUDE_DIR}
                --libdir=${LIBURING_INSTALL_DIR}/lib
                --libdevdir=${LIBURING_INSTALL_DIR}/lib
            WORKING_DIRECTORY ${LIBURING_SOURCE_DIR}
            RESULT_VARIABLE CONFIGURE_RESULT
        )
        if(NOT CONFIGURE_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to configure liburing ${LIBURING_VERSION}.")
        endif()

        if(CMAKE_BUILD_TYPE STREQUAL "debug")
            set(LIBURING_CFLAGS "-O0 -g -fPIC -fstack-protector-strong")
        else()
            set(LIBURING_CFLAGS "-O3 -g -fPIC -fstack-protector-strong -D_FORTIFY_SOURCE=2")
        endif()
        set(LIBURING_LDFLAGS "-Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now")

        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env
                "CC=${CMAKE_C_COMPILER}"
                "CFLAGS=${LIBURING_CFLAGS}"
                "LDFLAGS=${LIBURING_LDFLAGS}"
                make -C src -j8
            WORKING_DIRECTORY ${LIBURING_SOURCE_DIR}
            RESULT_VARIABLE BUILD_RESULT
        )
        if(NOT BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to build liburing ${LIBURING_VERSION}.")
        endif()

        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env
                "CC=${CMAKE_C_COMPILER}"
                "CFLAGS=${LIBURING_CFLAGS}"
                "LDFLAGS=${LIBURING_LDFLAGS}"
                make -C src install
                prefix=${LIBURING_INSTALL_DIR}
                includedir=${LIBURING_INCLUDE_DIR}
                libdir=${LIBURING_INSTALL_DIR}/lib
                libdevdir=${LIBURING_INSTALL_DIR}/lib
            WORKING_DIRECTORY ${LIBURING_SOURCE_DIR}
            RESULT_VARIABLE INSTALL_RESULT
        )
        if(NOT INSTALL_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to install liburing ${LIBURING_VERSION}.")
        endif()
    endif()

    if(NOT EXISTS "${LIBURING_LIBRARY}" OR NOT EXISTS "${LIBURING_INCLUDE_DIR}/liburing.h")
        message(FATAL_ERROR "liburing ${LIBURING_VERSION} build output is incomplete.")
    endif()
    set(LIBURING_FINAL_INCLUDE_DIR "${LIBURING_INCLUDE_DIR}")
    set(LIBURING_FINAL_LIBRARY "${LIBURING_LIBRARY}")
endif()

if(NOT TARGET uring)
    add_library(uring SHARED IMPORTED GLOBAL)
    set_target_properties(uring PROPERTIES
        IMPORTED_LOCATION "${LIBURING_FINAL_LIBRARY}"
        IMPORTED_SONAME "liburing.so.2"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBURING_FINAL_INCLUDE_DIR}"
    )
endif()

set(LIBURING_LIBRARIES uring CACHE INTERNAL "liburing target")
set(LIBURING_INCLUDE_DIRS "${LIBURING_FINAL_INCLUDE_DIR}" CACHE INTERNAL "liburing include directories")
message(STATUS "liburing target defined: ${LIBURING_FINAL_LIBRARY}")
