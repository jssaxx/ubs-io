set(SOURCE_DIR ${PROJECT_3RDPARTY_SRC_DIR}/hseceasy/hseceasy/)
set(BUILD_DIR ${PROJECT_BUILD_PATH}/build/hseceasy/)
set(INSTALL_DIR ${PROJECT_3RDPARTY_BIN_DIR}/hseceasy)

set(CHECK_FILE_HSECEASY ${INSTALL_DIR}/hse/cryption/include/cryption/hse_cryptor.h)

message("-- hseceasy: check if need to build at ${CHECK_FILE_HSECEASY}")

# build the lib if not built yet
if(EXISTS ${CHECK_FILE_HSECEASY})
    message("-- hseceasy: ${CHECK_FILE_HSECEASY}")
    message("-- hseceasy: has been built, ignored")
else()
    # create build dir
    exec_program(mkdir ${BUILD_DIR} ARGS -p ${BUILD_DIR})
    exec_program(mkdir ${BUILD_DIR} ARGS -p ${INSTALL_DIR})

    # configure
    #hseceasy 需要大写的Debug
    if(${CMAKE_BUILD_TYPE} STREQUAL "debug")
        exec_program(cmake ${BUILD_DIR} ARGS -DCMAKE_DEPENDS_USE_COMPILER=false -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=off -DBUILD_CRYPTION_TOOL=ON -DBUILD_TOOL_KEY_ACTIVE=ON -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} ${SOURCE_DIR})
    else()
        exec_program(cmake ${BUILD_DIR} ARGS -DCMAKE_DEPENDS_USE_COMPILER=false -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DBUILD_TESTS=off -DBUILD_CRYPTION_TOOL=ON -DBUILD_TOOL_KEY_ACTIVE=ON -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} ${SOURCE_DIR})
    endif()

    # execute make && make install
    exec_program(make ${BUILD_DIR} ARGS clean)
    exec_program(make ${BUILD_DIR} ARGS -j8)
    exec_program(make ${BUILD_DIR} ARGS install)

endif()