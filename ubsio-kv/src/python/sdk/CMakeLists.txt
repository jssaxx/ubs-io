cmake_minimum_required(VERSION 3.16)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 REQUIRED)
pybind11_add_module(c2python_sdk c2python.cpp)

target_include_directories(c2python_sdk PRIVATE
    ${Python3_INCLUDE_DIRS}
    ${SRC_DIR}/dfc/csrc/common/utils
    ${SRC_DIR}/dfc/csrc/client/kv/kv_client
    ${SRC_DIR}/dfc/csrc/client/kv/nds
    ${TARGET_INSTALL_LIB}/3rdparty/nds/include
)

target_link_directories(c2python_sdk PRIVATE
    ${TARGET_INSTALL_LIB}/bio
    ${TARGET_INSTALL_LIB}/../src/bio/csrc/boostio/output/boostio/lib
    ${TARGET_INSTALL_LIB}/openssl
    ${TARGET_INSTALL_LIB}/zookeeper
    ${TARGET_INSTALL_LIB}/hcom
)

target_link_libraries(c2python_sdk PRIVATE
    -Wl,--start-group
    pthread dl rt
    dfc_client
    dfc_nds
    ${Python3_LIBRARIES}
    -Wl,--end-group
)