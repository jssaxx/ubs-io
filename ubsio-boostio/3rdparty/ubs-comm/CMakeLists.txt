set(SOURCE_DIR ${PROJECT_3RDPARTY_SRC_DIR}/ubs-comm/ubs-comm)
set(BUILD_DIR ${PROJECT_BUILD_PATH}/build/ubs-comm/)
set(INSTALL_DIR ${PROJECT_3RDPARTY_BIN_DIR}/ubs-comm)

set(CHECK_FILE_HCOM ${INSTALL_DIR}/include/hcom/hcom.h)

find_path(HCOM_SYSTEM_INCLUDE_DIR
        NAMES hcom/hcom.h
        PATHS /usr/include
        NO_DEFAULT_PATH
)

find_library(HCOM_SYSTEM_SHARED_LIBRARY
        NAMES hcom libhcom.so
        PATHS /usr/lib64 /usr/lib
        NO_DEFAULT_PATH
)

if (HCOM_SYSTEM_INCLUDE_DIR AND HCOM_SYSTEM_SHARED_LIBRARY)
    message(STATUS "Found system-installed HCOM")
    set(USE_SYSTEM_HCOM ON)
    set(HCOM_FINAL_INCLUDE_DIR ${HCOM_SYSTEM_INCLUDE_DIR})
    set(HCOM_FINAL_LIBRARY ${HCOM_SYSTEM_SHARED_LIBRARY})
else ()
    message(STATUS "HCOM not found in system. Building from source...")
    set(USE_SYSTEM_HCOM OFF)
endif ()

if (NOT USE_SYSTEM_HCOM)
    set(ubs_comm_SOURCE_DIR ${SOURCE_DIR})
    if(NOT EXISTS "${SOURCE_DIR}/CMakeLists.txt")
        include(FetchContent)
        FetchContent_Declare(
                ubs_comm
                GIT_REPOSITORY https://gitcode.com/openeuler/ubs-comm.git
                GIT_TAG tag_BeiMing-I26.0.RC1.B013
                SOURCE_DIR ${SOURCE_DIR}
        )
        FetchContent_GetProperties(ubs_comm)
        if(NOT ubs_comm_POPULATED)
            FetchContent_Populate(ubs_comm)
        endif()
    endif()

    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm)")
        set(ENABLE_KP_OPT on)
    else()
        set(ENABLE_KP_OPT off)
    endif()

    if(NOT EXISTS ${CHECK_FILE_HCOM})
        file(MAKE_DIRECTORY ${BUILD_DIR})

        execute_process(
                COMMAND ${CMAKE_COMMAND}
                -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                -DBUILD_TESTS=off
                -DENABLE_ARM_KP=${ENABLE_KP_OPT}
                -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR}
                ${ubs_comm_SOURCE_DIR}
                WORKING_DIRECTORY ${BUILD_DIR}
        )
        execute_process(COMMAND make -j8 WORKING_DIRECTORY ${BUILD_DIR})
        execute_process(COMMAND make install WORKING_DIRECTORY ${BUILD_DIR})
    endif()

    set(HCOM_FINAL_INCLUDE_DIR "${INSTALL_DIR}/include")
    set(HCOM_FINAL_LIBRARY "${INSTALL_DIR}/lib/libhcom_static.a")
endif ()

if(NOT TARGET hcom_lib)
    add_library(hcom_lib STATIC IMPORTED GLOBAL)

    set_target_properties(hcom_lib PROPERTIES
            IMPORTED_LOCATION "${HCOM_FINAL_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${HCOM_FINAL_INCLUDE_DIR}"
    )

    set(HCOM_LIBRARIES hcom_lib CACHE INTERNAL "HCOM libraries")
    set(HCOM_INCLUDE_DIRS "${HCOM_FINAL_INCLUDE_DIR}" CACHE INTERNAL "HCOM includes")
endif()

message(STATUS "HCOM Target defined. Lib: ${HCOM_FINAL_LIBRARY}")
