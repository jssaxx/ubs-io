set(BUILD_DIR ${PROJECT_BUILD_PATH}/build/ubs-comm/)
set(INSTALL_DIR ${PROJECT_3RDPARTY_BIN_DIR}/ubs-comm)
set(HCOM_INCLUDE_DIR ${INSTALL_DIR}/include/hcom)
set(MULTICAST_INCLUDE_DIR ${HCOM_INCLUDE_DIR}/multicast)
set(CHECK_FILE_HCOM ${INSTALL_DIR}/include/hcom/hcom.h)

message("-- hcom: check if need to build at ${CHECK_FILE_HCOM}")

# build the lib if not built yet
if(EXISTS ${CHECK_FILE_HCOM})
    message("-- hcom: ${CHECK_FILE_HCOM}")
    message("-- hcom: has been built, ignored")
else()
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

    file(COPY ${MULTICAST_INCLUDE_DIR}/ DESTINATION ${HCOM_INCLUDE_DIR})
    file(REMOVE_RECURSE ${MULTICAST_INCLUDE_DIR})
endif()
