#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# script for Huawei ubsio-kv to build pkg
# version: 1.0.0
# ***********************************************************************

set -e
usage() {
    echo "Usage: $0 [ -h | -help ] [ -t | -type <build_type> ] [--ut=UT] [--build_boostio <ON|OFF>] [--build_python <ON|OFF>] [--origin_runpath <ON|OFF>]"
    echo "build_type: [debug, release, clean]"
    echo "Examples:"
    echo " 1 ./build.sh -t release"
    echo " 2 ./build.sh -t debug"
    echo " 3 ./build.sh -t debug --ut"
    echo " 4 ./build.sh -t release --build_boostio ON // Build ubsio-boostio before ubsio-kv"
    echo " 5 ./build.sh -t release --build_python OFF // Skip Python binding and wheel packaging"
    echo
    exit 1;
}

CURRENT_PATH="$(dirname "${BASH_SOURCE[0]}")"
PROJ_DIR="$(realpath "${CURRENT_PATH}")"
BOOSTIO_DIR="$(realpath "${PROJ_DIR}/../ubsio-boostio")"
BOOSTIO_INCLUDE_DIR="${BOOSTIO_DIR}/src/sdk"
BUILD_DIR=${PROJ_DIR}/Build
BUILD_UT=OFF
BUILD_BOOSTIO=OFF
BUILD_PYTHON=ON
UBSIO_ENABLE_ORIGIN_RUNPATH=OFF
BUILD_TYPE=release
arch=$(uname -m)

if [ ! -d "${BUILD_DIR}" ]; then
    mkdir -p ${BUILD_DIR}
fi

while true; do
    case "$1" in
        -t | --type )
            type=$2
            type=${type,,}
            [[ "$type" != "debug" && "$type" != "release" && "$type" != "clean" ]] && echo "Invalid build type $2" && usage
            if [[ "$type" == 'debug' ]]; then
                BUILD_TYPE=debug
            elif [[ "$type" == 'release' ]]; then
                BUILD_TYPE=release
            elif [[ "$type" == 'clean' ]]; then
                BUILD_TYPE=clean
            fi
            shift 2
            ;;
        --ut )
            BUILD_UT=ON
            shift ;;
        --build_boostio )
            boostio_flag="$2"
            boostio_flag=${boostio_flag^^}
            [[ "$boostio_flag" != "ON" && "$boostio_flag" != "OFF" ]] && echo "Invalid build_boostio flag $2" && usage
            BUILD_BOOSTIO=$boostio_flag
            shift 2 ;;
        --build_python )
            python_flag="$2"
            python_flag=${python_flag^^}
            [[ "$python_flag" != "ON" && "$python_flag" != "OFF" ]] && echo "Invalid build_python flag $2" && usage
            BUILD_PYTHON=$python_flag
            shift 2 ;;
        --origin_runpath )
            origin_runpath_flag="${2^^}"
            [[ "$origin_runpath_flag" != "ON" && "$origin_runpath_flag" != "OFF" ]] && usage
            UBSIO_ENABLE_ORIGIN_RUNPATH=$origin_runpath_flag
            shift 2 ;;
        -h | -help )
            usage
            exit 0
            ;;
        * )
            break;;
    esac
done

if [[ "$BUILD_TYPE" == "clean" ]]; then
    cd $BUILD_DIR
    BUILD_CMD="make clean"
    echo
    echo $BUILD_CMD
    $BUILD_CMD || {
        echo "Failed to clean ubsio-kv."
    }

    echo
    echo "clean ubsio-kv successful."
    rm -rf ${PROJ_DIR}/dist
    rm -rf ${PROJ_DIR}/Build
    exit 0
fi

if [[ "$BUILD_BOOSTIO" == "ON" ]]; then
    echo "building ubsio-boostio first..."
    cd ${BOOSTIO_DIR}
    bash build.sh -t ${BUILD_TYPE} --build_kv OFF
    if [ $? -ne 0 ]; then
        echo "Failed to build ubsio-boostio."
        exit 1
    fi
    echo "ubsio-boostio build completed."
    cd ${PROJ_DIR}
fi

if [[ ! -f "${BOOSTIO_INCLUDE_DIR}/bio_c.h" ]]; then
    echo "bio_c.h not found at ${BOOSTIO_INCLUDE_DIR}."
    exit 1
fi

CMAKE_FLAGS=""
CMAKE_FLAGS+="-DUBSIO_BOOSTIO_INCLUDE_DIR=${BOOSTIO_INCLUDE_DIR} "
CMAKE_FLAGS+="-DBUILD_PYTHON=${BUILD_PYTHON} "
CMAKE_FLAGS+="-DUBSIO_ENABLE_ORIGIN_RUNPATH=${UBSIO_ENABLE_ORIGIN_RUNPATH} "

if [[ "$BUILD_UT" == 'ON' ]]; then
    if [[ -z "${TEST_TOOL_PATH:-}" ]]; then
        TEST_TOOL_PATH="${PROJ_DIR}/dist/kv_test_tools"
    fi
    if [[ ! -f "${TEST_TOOL_PATH}/googletest/include/gtest/gtest.h" ]]; then
        echo "ubsio-kv test tools are not installed, installing..."
        KV_TEST_TOOL_PATH="${TEST_TOOL_PATH}" bash "${PROJ_DIR}/build/install_test_tools.sh"
    fi
    CMAKE_FLAGS+="-DDEBUG_UT=ON "
    CMAKE_FLAGS+="-DTEST_TOOL_INSTALL_PATH=${TEST_TOOL_PATH} "
else
    CMAKE_FLAGS+="-DDEBUG_UT=OFF "
fi

CPU_PROCESSOR_NUM=$(nproc)
CMAKE_CMD="cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE $CMAKE_FLAGS $PROJ_DIR"
BUILD_CMD="make install -j ${CPU_PROCESSOR_NUM}"

cd $BUILD_DIR
echo $CMAKE_CMD
$CMAKE_CMD || {
    echo "Failed to configure ubsio-kv build."
    exit 1
}
echo $BUILD_CMD
$BUILD_CMD || {
    echo "Failed to build ubsio-kv."
    exit 1
}

if [[ "$BUILD_PYTHON" == "ON" ]]; then
    # PYTHON_HOME后续按照正式编译工程适配 /opt/buildtools/python-3.10
    if pip3 show wheel; then
        echo "wheel has been installed"
    else
        echo "wheel installing"
        pip3 install wheel
    fi

    \cp -v ${PROJ_DIR}/Build/src/python/sdk/c2python_sdk.cpython*.so ${PROJ_DIR}/python_whl/pykvc/pykvc/

    cd ${PROJ_DIR}/python_whl/pykvc/
    rm -rf build/
    rm -rf dist/
    rm -rf *.egg-info/
    python3 setup.py bdist_wheel --py-limited-api=cp37

    mkdir -p ${PROJ_DIR}/dist/pkg
    wheel_file=$(find ${PROJ_DIR}/python_whl/pykvc/dist -maxdepth 1 -name 'pykvc-1.0.0-cp37-abi3-linux_*.whl' | head -n 1)
    if [[ -z "${wheel_file}" ]]; then
        echo "Failed to find pykvc wheel package."
        exit 1
    fi
    \cp -rf ${wheel_file} ${PROJ_DIR}/dist/pkg
fi

echo ""
echo "Build completed successfully!"
echo "Output files:"
if [[ "$BUILD_PYTHON" == "ON" ]]; then
    echo "  Whl pkg: ${PROJ_DIR}/dist/pkg/"
fi
echo "  Library: ${PROJ_DIR}/dist/lib/"
echo "  Header:  ${PROJ_DIR}/dist/include/"
