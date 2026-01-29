set(SOURCE_DIR ${PROJECT_3RDPARTY_SRC_DIR}/libboundscheck/libboundscheck)
set(OBJECT_DIR ${PROJECT_3RDPARTY_SRC_DIR}/libboundscheck/libboundscheck/obj)
set(BUILD_DIR ${PROJECT_BUILD_PATH}/3rdparty/libboundscheck/)
set(INSTALL_DIR ${PROJECT_3RDPARTY_BIN_DIR}/libboundscheck)

message("-- secure: check if need to build")

find_file(LIBS_BUILT_SECURE_C libboundscheck.a ${INSTALL_DIR}/lib/)

# build the lib if not built yet
if(NOT EXISTS ${LIBS_BUILT_SECURE_C})
    execute_process(COMMAND make -j WORKING_DIRECTORY ${SOURCE_DIR})
    EXEC_PROGRAM(ar ${INSTALL_DIR}/lib ARGS crv ${INSTALL_DIR}/lib/libboundscheck.a ${OBJECT_DIR}/*.o)
    execute_process(COMMAND mkdir -p ${INSTALL_DIR} WORKING_DIRECTORY ${SOURCE_DIR})
    execute_process(COMMAND cp -r ${CMAKE_CURRENT_SOURCE_DIR}/libboundscheck/include ${INSTALL_DIR} WORKING_DIRECTORY ${INSTALL_DIR})
else()
    message("-- secure: ${LIBS_BUILT_SECURE_C}")
    message("-- secure: has been built, ignored")
endif(NOT EXISTS ${LIBS_BUILT_SECURE_C})

