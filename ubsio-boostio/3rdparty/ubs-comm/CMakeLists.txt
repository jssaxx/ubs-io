# Use master for the initial download; reuse any existing source checkout.
set(HCOM_BRANCH master)
set(SOURCE_DIR ${PROJECT_3RDPARTY_SRC_DIR}/ubs-comm/ubs-comm)
set(BUILD_DIR ${PROJECT_BUILD_PATH}/build/ubs-comm)
set(INSTALL_DIR ${PROJECT_3RDPARTY_BIN_DIR}/ubs-comm)

if(NOT EXISTS "${SOURCE_DIR}/CMakeLists.txt")
    include(FetchContent)
    FetchContent_Declare(ubs_comm
        GIT_REPOSITORY https://gitcode.com/openeuler/ubs-comm.git
        GIT_TAG ${HCOM_BRANCH}
        SOURCE_DIR ${SOURCE_DIR})
    FetchContent_GetProperties(ubs_comm)
    if(NOT ubs_comm_POPULATED)
        FetchContent_Populate(ubs_comm)
    endif()
endif()

# Existing checkouts and offline archives may use another compatible revision.
# Compile BoostIO against the headers installed with that source's runtime.
foreach(HCOM_HEADER hcom_service_c.h hcom_c.h)
    if(NOT EXISTS "${SOURCE_DIR}/src/hcom/api/capi_v2/${HCOM_HEADER}")
        message(FATAL_ERROR "HCOM C API header ${HCOM_HEADER} is missing. Prepare compatible HCOM sources first.")
    endif()
endforeach()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm)")
    set(ENABLE_KP_OPT ON)
else()
    set(ENABLE_KP_OPT OFF)
endif()

get_target_property(HCOM_BOUNDSCHECK_INCLUDE boundscheck_lib INTERFACE_INCLUDE_DIRECTORIES)
find_library(HCOM_BOUNDSCHECK_LIBRARY NAMES boundscheck
    PATHS /usr/lib64 /usr/lib ${PROJECT_3RDPARTY_BIN_DIR}/libboundscheck/lib)
if(NOT HCOM_BOUNDSCHECK_LIBRARY)
    message(FATAL_ERROR "HCOM requires the BoostIO boundscheck runtime")
endif()
file(MAKE_DIRECTORY ${BUILD_DIR})
# Configure on every BoostIO configure so changed feature flags cannot reuse a
# runtime built with incompatible options. The actual compilation is incremental.
execute_process(COMMAND ${CMAKE_COMMAND}
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DBUILD_TESTS=OFF -DBUILD_HCOM=ON -DBUILD_UBSOCKET=OFF
    -DENABLE_ARM_KP=${ENABLE_KP_OPT} -DBUILD_WITH_RDMA=${UBSIO_HCOM_ENABLE_RDMA}
    -DLIBBOUNDSCHECK_INCLUDE_DIR=${HCOM_BOUNDSCHECK_INCLUDE}
    -DLIBBOUNDSCHECK_SHARED_LIBRARY=${HCOM_BOUNDSCHECK_LIBRARY}
    -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} ${SOURCE_DIR}
    WORKING_DIRECTORY ${BUILD_DIR} RESULT_VARIABLE HCOM_CONFIG_RESULT)
if(NOT HCOM_CONFIG_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to configure HCOM")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} --build ${BUILD_DIR} --parallel 8
    RESULT_VARIABLE HCOM_BUILD_RESULT)
if(NOT HCOM_BUILD_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to build HCOM")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} --install ${BUILD_DIR}
    RESULT_VARIABLE HCOM_INSTALL_RESULT)
if(NOT HCOM_INSTALL_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to install HCOM")
endif()

add_library(hcom_headers INTERFACE)
target_include_directories(hcom_headers INTERFACE ${INSTALL_DIR}/include)
# The runtime is loaded with dlopen, never linked into BoostIO.
install(DIRECTORY ${INSTALL_DIR}/lib/ DESTINATION ${TARGET_INSTALL_LIB_DIR}
    FILES_MATCHING PATTERN "libhcom.so*")
