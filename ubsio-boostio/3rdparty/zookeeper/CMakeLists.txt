set(SOURCE_DIR ${PROJECT_3RDPARTY_SRC_DIR}/zookeeper/zookeeper/zookeeper-client/zookeeper-client-c/)
set(INSTALL_DIR ${PROJECT_3RDPARTY_BIN_DIR}/zookeeper)

find_file(LIBS_BUILT_ZK libzookeeper_mt.so ${INSTALL_DIR}/lib/)
if(EXISTS ${LIBS_BUILT_ZK})
    message("-- zookeeper: ${LIBS_BUILT_ZK}")
    message("-- zookeeper: has been built, ignored")
else()
    # create build dir
    exec_program(mkdir ${SOURCE_DIR} ARGS -p ${INSTALL_DIR})

    # mvn install
    message("mvn install")
    execute_process(COMMAND mvn clean install -DskipTests
            WORKING_DIRECTORY ${SOURCE_DIR}/../../zookeeper-jute)
    message("make install")
	
	execute_process(COMMAND autoreconf -if WORKING_DIRECTORY ${SOURCE_DIR})

    execute_process(COMMAND ./configure --without-cppunit --prefix=${INSTALL_DIR} WORKING_DIRECTORY ${SOURCE_DIR})

    # execute make && make install
    exec_program(make ${SOURCE_DIR} ARGS clean)
    exec_program(make ${SOURCE_DIR} ARGS -j8)
    exec_program(make ${SOURCE_DIR} ARGS install)

    if (NOT OPEN_RELEASE STREQUAL "ON")
        exec_program(cp ${TARGET_INSTALL_LIB_DIR} ARGS -rf ${INSTALL_DIR}/lib/*.so ${TARGET_INSTALL_LIB_DIR})
    endif()
endif()