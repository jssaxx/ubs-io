set(SOURCE_DIR ${PROJECT_3RDPARTY_SRC_DIR}/zookeeper/zookeeper/zookeeper-client/zookeeper-client-c/)
set(INSTALL_DIR ${PROJECT_3RDPARTY_BIN_DIR}/zookeeper)

find_file(LIBS_BUILT_ZK libzookeeper_mt.so ${INSTALL_DIR}/lib/)
if(NOT LIBS_BUILT_ZK)
    include(FetchContent)
    FetchContent_Declare(
            zookeeper
            GIT_REPOSITORY https://github.com/apache/zookeeper.git
            GIT_TAG release-3.9.5
            SOURCE_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/zookeeper"
    )
    FetchContent_GetProperties(zookeeper)
    if(NOT zookeeper_POPULATED)
        FetchContent_Populate(zookeeper)
    endif()

    # create build dir
    exec_program(mkdir ${SOURCE_DIR} ARGS -p ${INSTALL_DIR})

    # mvn install
    message("mvn install")
    execute_process(COMMAND mvn clean install -DskipTests
            WORKING_DIRECTORY ${SOURCE_DIR}/../../zookeeper-jute)
    message("make install")

    # change Makefile.am
    execute_process(COMMAND autoreconf -if WORKING_DIRECTORY ${SOURCE_DIR})

    # change configure
    set(PLATFORM_CFLAGS  $ENV{CFLAGS})
    set(PLATFORM_CXXFLAGS $ENV{CXXFLAGS})
    set(PLATFORM_LDFLAGS $ENV{LDFLAGS})
    set(ENV{CFLAGS} "-fstack-protector-strong  -fPIE")
    set(ENV{CXXFLAGS} "-fstack-protector-strong -fPIE")
    set(ENV{LDFLAGS} "-Wl,-z,relro,-z,now,-z,noexecstack,-s")
    execute_process(COMMAND ./configure --without-cppunit --prefix=${INSTALL_DIR}
            WORKING_DIRECTORY ${SOURCE_DIR}
            )

    execute_process(COMMAND make clean WORKING_DIRECTORY ${SOURCE_DIR})
    execute_process(COMMAND make -j8 WORKING_DIRECTORY ${SOURCE_DIR})
    execute_process(COMMAND make install -j8 WORKING_DIRECTORY ${SOURCE_DIR})
    set(ENV{CFLAGS} ${PLATFORM_CFLAGS})
    set(ENV{CXXFLAGS} ${PLATFORM_CXXFLAGS})
    set(ENV{LDFLAGS} ${PLATFORM_LDFLAGS})
else()
    message("-- zookeeper: ${LIBS_BUILT_ZK}")
    message("-- zookeeper: has been built, ignored")
endif(NOT LIBS_BUILT_ZK)
