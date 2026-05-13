set(BUILD_DIR ${PROJECT_BUILD_PATH}/build/ubs-comm/)
set(INSTALL_DIR ${PROJECT_3RDPARTY_BIN_DIR}/ubs-comm)
set(HCOM_INCLUDE_DIR ${INSTALL_DIR}/include/hcom)
set(MULTICAST_INCLUDE_DIR ${HCOM_INCLUDE_DIR}/multicast)
set(CHECK_FILE_HCOM ${INSTALL_DIR}/include/hcom/hcom.h)
set(CHECK_FILE_HCOM_MULTICAST ${MULTICAST_INCLUDE_DIR}/multicast_publisher.h)

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
else()
    message(STATUS "HCOM not found in system. Building from source...")
    set(USE_SYSTEM_HCOM OFF)
endif()

if(NOT USE_SYSTEM_HCOM)
    include(FetchContent)
    FetchContent_Declare(
            ubs_comm
            GIT_REPOSITORY https://gitcode.com/openeuler/ubs-comm.git
            GIT_TAG tag_BeMing_Huatai_430_Poc_01
            SOURCE_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/ubs_comm"
    )
    FetchContent_GetProperties(ubs_comm)
    if(NOT ubs_comm_POPULATED)
        FetchContent_Populate(ubs_comm)
        set(SOURCE_DIR ${ubs_comm_SOURCE_DIR})
        message("down ubs-comm to ${SOURCE_DIR}")
    endif()

    if(NOT EXISTS ${CHECK_FILE_HCOM} OR NOT EXISTS ${CHECK_FILE_HCOM_MULTICAST})
        # create build dir
        exec_program(mkdir ${BUILD_DIR} ARGS -p ${BUILD_DIR})
        exec_program(mkdir ${BUILD_DIR} ARGS -p ${INSTALL_DIR})

        if(CMAKE_BUILD_TYPE STREQUAL "release")
            set(EXTRA_LINKER_FLAGS "-s")
        else()
            set(EXTRA_LINKER_FLAGS "")
        endif()

        # configure
        exec_program(cmake ${BUILD_DIR} ARGS
                -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                -DCMAKE_SHARED_LINKER_FLAGS="${EXTRA_LINKER_FLAGS}"
                -DBUILD_WITH_MULTICAST=on
                -DBUILD_WITH_HW_CRC=on
                -DCMAKE_DEPENDS_USE_COMPILER=false
                -DBUILD_TESTS=off
                -DENABLE_ARM_KP=on
                -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} ${SOURCE_DIR})

        # execute make && make install
        exec_program(make ${BUILD_DIR} ARGS clean)
        exec_program(make ${BUILD_DIR} ARGS -j8)
        exec_program(make ${BUILD_DIR} ARGS install)
    endif()

    file(GLOB FLAT_MULTICAST_HEADERS ${HCOM_INCLUDE_DIR}/multicast*.h)
    if(FLAT_MULTICAST_HEADERS)
        file(REMOVE ${FLAT_MULTICAST_HEADERS})
    endif()

    set(HCOM_FINAL_INCLUDE_DIR "${INSTALL_DIR}/include")
    set(HCOM_FINAL_LIBRARY "${INSTALL_DIR}/lib/libhcom.so")
endif()

if(NOT TARGET hcom_lib)
    add_library(hcom_lib SHARED IMPORTED GLOBAL)

    set_target_properties(hcom_lib PROPERTIES
            IMPORTED_LOCATION "${HCOM_FINAL_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${HCOM_FINAL_INCLUDE_DIR}"
    )

    set(HCOM_LIBRARIES hcom_lib CACHE INTERNAL "HCOM libraries")
    set(HCOM_INCLUDE_DIRS "${HCOM_FINAL_INCLUDE_DIR}" CACHE INTERNAL "HCOM includes")
endif()

message(STATUS "HCOM Target defined. Lib: ${HCOM_FINAL_LIBRARY}")
