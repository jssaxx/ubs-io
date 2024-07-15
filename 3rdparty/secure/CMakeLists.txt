#build secure
set(SOURCE_DIR ${PROJECT_3RDPARTY_SRC_DIR}/secure/huawei_secure_c)
set(BUILD_DIR ${PROJECT_BUILD_PATH}/3rdparty/secure/)
set(INSTALL_DIR ${PROJECT_3RDPARTY_BIN_DIR}/huawei_secure_c)

message("-- secure: check if need to build")

# build the lib if not built yet
find_file(LIBS_BUILT_SECURE_C libsecurec.so ${INSTALL_DIR}/lib/)
# check if the file exists or not, if not build it
if(NOT EXISTS ${LIBS_BUILT_SECURE_C})
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 11)

    exec_program(cp ${BUILD_DIR} ARGS -rf ${SOURCE_DIR} ${BUILD_DIR})

    # create build dir
    exec_program(mkdir ${BUILD_PATH} ARGS -p ${BUILD_DIR})

    exec_program(make ${BUILD_DIR}/huawei_secure_c/src ARGS -j4)

    # create .a
    exec_program(ar ${INSTALL_DIR}/lib ARGS crv ${INSTALL_DIR}/lib/libsecurec.a ${BUILD_DIR}/huawei_secure_c/obj/*.o)

    # cp include and lib
    exec_program(cp ${INSTALL_DIR} ARGS -rf ${BUILD_DIR}/huawei_secure_c/include ${INSTALL_DIR})

else()
    message("-- secure: ${LIBS_BUILT_SECURE_C}")
    message("-- secure: has been built, ignored")
endif(NOT EXISTS ${LIBS_BUILT_SECURE_C})
