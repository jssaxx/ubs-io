set(SOURCE_DIR ${PROJECT_3RDPARTY_SRC_DIR}/ubs-comm/ubs-comm/)
set(BUILD_DIR ${PROJECT_BUILD_PATH}/build/ubs-comm/)
set(INSTALL_DIR ${PROJECT_3RDPARTY_BIN_DIR}/ubs-comm)

set(CHECK_FILE_HCOM ${INSTALL_DIR}/include/hcom/hcom.h)

message("-- hcom: check if need to build at ${CHECK_FILE_HCOM}")

if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm)")
    set(ENABLE_KP_OPT on)
else()
    set(ENABLE_KP_OPT off)
endif()

# build the lib if not built yet
if(EXISTS ${CHECK_FILE_HCOM})
    message("-- hcom: ${CHECK_FILE_HCOM}")
    message("-- hcom: has been built, ignored")
else()
    # create build dir
    exec_program(mkdir ${BUILD_DIR} ARGS -p ${BUILD_DIR})
    exec_program(mkdir ${BUILD_DIR} ARGS -p ${INSTALL_DIR})

    # configure
    exec_program(cmake ${BUILD_DIR} ARGS
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_DEPENDS_USE_COMPILER=false
            -DBUILD_TESTS=off
            -DENABLE_ARM_KP=${ENABLE_KP_OPT}
            -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} ${SOURCE_DIR})

    # execute make && make install
    exec_program(make ${BUILD_DIR} ARGS clean)
    exec_program(make ${BUILD_DIR} ARGS -j8)
    exec_program(make ${BUILD_DIR} ARGS install)

    exec_program(cp ${TARGET_INSTALL_LIB_DIR} ARGS -rf ${INSTALL_DIR}/lib/libhcom.so ${TARGET_INSTALL_LIB_DIR})
endif()
