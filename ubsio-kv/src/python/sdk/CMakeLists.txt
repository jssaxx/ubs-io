cmake_minimum_required(VERSION 3.16)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 REQUIRED)
pybind11_add_module(c2python_sdk c2python.cpp)

set(UBSIO_BOOSTIO_DIR ${PROJECT_SOURCE_DIR}/../ubsio-boostio)
set(UBSIO_NDS_DIR ${PROJECT_SOURCE_DIR}/../ubsio-nds)

target_include_directories(c2python_sdk PRIVATE
    ${Python3_INCLUDE_DIRS}
    ${SRC_DIR}/csrc/kvc
    ${SRC_DIR}/csrc/nds
    ${SRC_DIR}/csrc/utils
    ${PROJECT_SOURCE_DIR}/include
    ${UBSIO_BOOSTIO_DIR}/src/sdk
    ${UBSIO_BOOSTIO_DIR}/src/common
    ${UBSIO_NDS_DIR}/include
    ${TARGET_INSTALL_LIB}/3rdparty/nds/include
)

target_link_libraries(c2python_sdk PRIVATE
    -Wl,--start-group
    pthread dl rt
    ubsio_kvc
    ${Python3_LIBRARIES}
    -Wl,--end-group
)