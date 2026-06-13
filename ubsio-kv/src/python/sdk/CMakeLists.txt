cmake_minimum_required(VERSION 3.16)

find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

execute_process(
    COMMAND ${Python3_EXECUTABLE} -c
    "import sys; import os; import pybind11; print(os.path.dirname(pybind11.__file__))"
    OUTPUT_VARIABLE PYTHON_PYBIND11_HOME
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(pybind11_cmake_dir "${PYTHON_PYBIND11_HOME}/share/cmake/pybind11")

if (NOT EXISTS "${pybind11_cmake_dir}" OR NOT IS_DIRECTORY "${pybind11_cmake_dir}")
    message(FATAL_ERROR "pybind11 cmake config not found at: ${pybind11_cmake_dir}\n"
                        "Please install pybind11 via pip: pip3 install pybind11")
endif ()

# preserve existing CMAKE_PREFIX_PATH from both cmake variable and environment
if (DEFINED ENV{CMAKE_PREFIX_PATH})
    list(APPEND CMAKE_PREFIX_PATH $ENV{CMAKE_PREFIX_PATH})
endif ()
list(APPEND CMAKE_PREFIX_PATH "${pybind11_cmake_dir}")
message(STATUS "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")

find_package(pybind11 REQUIRED)
pybind11_add_module(c2python_sdk c2python.cpp)

set(UBSIO_BOOSTIO_DIR ${PROJECT_SOURCE_DIR}/../ubsio-boostio)

target_include_directories(c2python_sdk PRIVATE
    ${Python3_INCLUDE_DIRS}
    ${SRC_DIR}/csrc/kvc
    ${SRC_DIR}/csrc/nds
    ${SRC_DIR}/csrc/utils
    ${PROJECT_SOURCE_DIR}/include
    ${UBSIO_BOOSTIO_DIR}/src/sdk
    ${UBSIO_BOOSTIO_DIR}/src/common
)

target_link_directories(c2python_sdk PRIVATE
    ${UBSIO_BOOSTIO_DIR}/build
)

target_link_libraries(c2python_sdk PRIVATE
    -Wl,--start-group
    pthread dl rt
    ubsio_kvc
    ${Python3_LIBRARIES}
    -Wl,--end-group
)