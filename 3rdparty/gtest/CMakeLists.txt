#build gtest
set(SOURCE_DIR ${PROJECT_3RDPARTY_SRC_DIR}/gtest/gtest)
set(BUILD_DIR ${PROJECT_BUILD_PATH}/gtest)
set(INSTALL_DIR ${PROJECT_3RDPARTY_BIN_DIR}/gtest)

message("-- gtest: check if need to build ")

# build the lib if not built yet
find_file(LIBS_BUILT_GTEST libgtest.a ${INSTALL_DIR}/lib64/ ${INSTALL_DIR}/lib)
if(NOT EXISTS ${LIBS_BUILT_GTEST})

    # create build dir
    exec_program(mkdir ${BUILD_PATH} ARGS -p ${BUILD_DIR})

    # configure
    exec_program(cmake ${BUILD_DIR} ARGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} ${SOURCE_DIR})

    # execute make && make install
    exec_program(make ${BUILD_DIR} ARGS clean)
    exec_program(make ${BUILD_DIR} ARGS -j8)
    exec_program("make install" ${BUILD_DIR})

else()
    message("-- gtest: ${LIBS_BUILT_GTEST}")
    message("-- gtest: has been built, ignored")
endif(NOT EXISTS ${LIBS_BUILT_GTEST})
