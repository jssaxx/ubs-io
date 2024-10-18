set(SOURCE_DIR ${PROJECT_3RDPARTY_SRC_DIR}/openssl/openssl-1.1.1n)
set(BUILD_DIR ${PROJECT_3RDPARTY_BIN_DIR}/openssl)
set(LIBOPENSSL_INSTALL_DIR ${PROJECT_3RDPARTY_BIN_DIR}/openssl)

message("-- openssl : check if need to build ")

# build the lib if not built yet
find_file(LIBS_BUILT_LIBOPENSSL libssl.so ${LIBOPENSSL_INSTALL_DIR}/lib/)
if(NOT LIBS_BUILT_LIBOPENSSL)

    # create build dir
    exec_program(mkdir ${PROJECT_3RDPARTY_BIN_DIR} ARGS -p ${BUILD_DIR})


    # configure
    exec_program(chmod ${SOURCE_DIR} ARGS +x config)
    exec_program(dos2unix ${SOURCE_DIR} ARGS config)
    exec_program(./config ${SOURCE_DIR} ARGS --prefix=${LIBOPENSSL_INSTALL_DIR}
                 CFLAGS="-fstack-protector-strong"
                 CXXFLAGS="-fstack-protector-strong"
                 LDFLAGS="-Wl,-z,now -s"
    )

    # execute make && make install
    exec_program(make ${SOURCE_DIR} ARGS clean)
    exec_program(make ${SOURCE_DIR} ARGS -j4)
    exec_program(make ${SOURCE_DIR} ARGS install)
else()
    message("-- openssl: ${LIBS_BUILT_LIBOPENSSL}")
    message("-- openssl: has been built, ignored")
endif(NOT LIBS_BUILT_LIBOPENSSL)
