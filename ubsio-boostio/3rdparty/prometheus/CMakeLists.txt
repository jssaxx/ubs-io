set(SOURCE_DIR ${PROJECT_3RDPARTY_SRC_DIR}/prometheus/prometheus/)
set(BUILD_DIR ${PROJECT_BUILD_PATH}/build/prometheus/)
set(INSTALL_DIR ${PROJECT_3RDPARTY_BIN_DIR}/prometheus)

set(CHECK_FILE_PROMETHEUS ${INSTALL_DIR}/include/prometheus/gauge.h)

message("-- prometheus: check if need to build at ${CHECK_FILE_PROMETHEUS}")

# build the lib if not built yet
if(EXISTS ${CHECK_FILE_PROMETHEUS})
    message("-- prometheus: ${CHECK_FILE_PROMETHEUS}")
    message("-- prometheus: has been built, ignored")
else()
    set(prometheus_SOURCE_DIR ${SOURCE_DIR})
    if(NOT EXISTS "${SOURCE_DIR}/CMakeLists.txt")
        include(FetchContent)

        FetchContent_Declare(
                prometheus
                GIT_REPOSITORY https://gitcode.com/gh_mirrors/pr/prometheus-cpp.git
                GIT_TAG v1.2.4
                SOURCE_DIR ${SOURCE_DIR}
        )

        # 获取 prometheus 源码路径
        FetchContent_GetProperties(prometheus)
        if(NOT prometheus_POPULATED)
            FetchContent_Populate(prometheus)
        endif()
    endif()
    # create build dir
    exec_program(mkdir ${BUILD_DIR} ARGS -p ${BUILD_DIR})
    exec_program(mkdir ${BUILD_DIR} ARGS -p ${INSTALL_DIR})

    # configure
    exec_program(cmake ${BUILD_DIR} ARGS -DBUILD_SHARED_LIBS=ON -DENABLE_TESTING=OFF -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} ${prometheus_SOURCE_DIR})

    # execute make && make install
    exec_program(make ${BUILD_DIR} ARGS clean)
    exec_program(make ${BUILD_DIR} ARGS -j8)
    exec_program(make ${BUILD_DIR} ARGS install)

endif()
