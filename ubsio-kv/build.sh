#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# script for Huawei ubsio-kv to build pkg
# version: 1.0.0
# ***********************************************************************

set -e
usage() {
    echo "Usage: $0 [ -h | -help ] [ -t | -type <build_type> ] [--ut=UT]"
    echo "build_type: [debug, release, clean]"
    echo "Examples:"
    echo " 1 ./build.sh -t release"
    echo " 2 ./build.sh -t debug"
    echo " 3 ./build.sh -t debug --ut"
    echo
    exit 1;
}

CURRENT_PATH="$(dirname "${BASH_SOURCE[0]}")"
PROJ_DIR="$(realpath "${CURRENT_PATH}")"
BOOSTIO_DIR="$(realpath "${PROJ_DIR}/../ubsio-boostio")"
BUILD_DIR=${PROJ_DIR}/Build
BUILD_UT=OFF
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

if [[ ! -d "${BOOSTIO_DIR}/dist/boostio/lib" ]]; then
    echo "ubsio-boostio not found, building ubsio-boostio first..."
    cd ${BOOSTIO_DIR}
    bash build.sh -t ${BUILD_TYPE}
    if [ $? -ne 0 ]; then
        echo "Failed to build ubsio-boostio."
        exit 1
    fi
    echo "ubsio-boostio build completed."
    cd ${PROJ_DIR}
fi

CMAKE_FLAGS=""

if [[ "$BUILD_UT" == 'ON' ]]; then
    CMAKE_FLAGS+="-DDEBUG_UT=ON "
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
\cp -rf ${PROJ_DIR}/python_whl/pykvc/dist/pykvc-1.0.0-cp37-abi3-linux_aarch64.whl ${PROJ_DIR}/dist/pkg

echo ""
echo "Build completed successfully!"
echo "Output files:"
echo "  Whl pkg: ${PROJ_DIR}/dist/pkg/"
echo "  Library: ${PROJ_DIR}/dist/lib/"
echo "  Header:  ${PROJ_DIR}/dist/include/"
