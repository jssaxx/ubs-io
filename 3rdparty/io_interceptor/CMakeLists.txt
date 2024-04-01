set(SOURCE_DIR ${PROJECT_3RDPARTY_SRC_DIR}/io_interceptor/io_interceptor/)
set(BUILD_DIR ${PROJECT_BUILD_PATH}/build/io_interceptor/)
set(INSTALL_DIR ${PROJECT_3RDPARTY_BIN_DIR}/io_interceptor)

set(CHECK_FILE_INTERCEPTOR ${INSTALL_DIR}/lib/libock_interceptor.so)

message("-- io_interceptor: check if need to build at ${CHECK_FILE_INTERCEPTOR}")

# build the lib if not built yet
if(EXISTS ${CHECK_FILE_INTERCEPTOR})
    message("-- io_interceptor: ${io_interceptor}")
    message("-- io_interceptor: has been built, ignored")
else()
    # create build dir
    exec_program(mkdir ${BUILD_DIR} ARGS -p ${BUILD_DIR})
    exec_program(mkdir ${BUILD_DIR} ARGS -p ${INSTALL_DIR})

    # configure
    exec_program(cmake ${BUILD_DIR} ARGS  -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_BUILD_DIR=${BUILD_DIR} -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} ${SOURCE_DIR})

    # execute make && make install
    exec_program(make ${BUILD_DIR} ARGS clean)
    exec_program(make ${BUILD_DIR} ARGS -j8)
    exec_program(make ${BUILD_DIR} ARGS install)

    exec_program(cp ${TARGET_INSTALL_LIB_DIR} ARGS -rf ${INSTALL_DIR}/lib/libock_interceptor.so ${TARGET_INSTALL_LIB_DIR})
endif()