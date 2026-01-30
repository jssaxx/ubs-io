#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# script for Huawei ubs-io to build pkg
# version: 1.0.0
# ***********************************************************************

set -e
usage() {
    echo "Usage: $0 [ -h | -help ] [ -t | -type <build_type> ] [--cli=Diagnose] [--ut=UT] [--tp=tracepoint] [--pms=prometheus]"
    echo "build_type: [debug, release, clean]"
    echo "Examples:"
    echo " 1 ./build.sh -t release [--cli] // 禁止添加tp功能，对外发布包禁止添加cli功能"
    echo " 2 ./build.sh -t debug // 默认添加cli和tp功能"
    echo " 3 ./build.sh -t debug [--ut] // 限制仅DT构建脚本使用"
    echo
    exit 1;
}

CURRENT_PATH="$(dirname "${BASH_SOURCE[0]}")"
PROJ_DIR="$(realpath "${CURRENT_PATH}")"
BUILD_DIR=${PROJ_DIR}/Build
BUILD_UT=OFF
CLI_FLAG=OFF
TP_FLAG=OFF
PROMETHEUS_FLAG=OFF
BUILD_TYPE=debug
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
			          TP_FLAG=ON
			          CLI_FLAG=ON
            elif [[ "$type" == 'release' ]]; then
                BUILD_TYPE=release
            elif [[ "$type" == 'clean' ]]; then
                BUILD_TYPE=clean
            fi
            shift 2
            ;;
		    --ut )
			      BUILD_UT=ON
            CLI_FLAG=ON
			      TP_FLAG=ON
            shift ;;
        --cli )
			      CLI_FLAG=ON
            shift ;;
        --tp )
            CLI_FLAG=ON
            TP_FLAG=ON
            shift ;;
        --pms )
            PROMETHEUS_FLAG=ON
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
        echo "Failed to clean boostio."
        exit 1
    }

    echo
    echo "clean boostio successful."
    rm -rf ${PROJ_DIR}/dist
    exit 0
fi

if [[ "$BUILD_TYPE" == "release" ]]; then
    TP_FLAG=OFF
    CMAKE_FLAGS+='-DOPEN_RELEASE=ON '
else
    CMAKE_FLAGS+='-DOPEN_RELEASE=OFF '
fi

if [[ "$CLI_FLAG" == "ON" ]]; then
    CMAKE_FLAGS+='-DOPEN_CLI=ON '
else
    CMAKE_FLAGS+='-DOPEN_CLI=OFF '
fi

if [[ "$TP_FLAG $arch" == 'ON aarch64' ]]; then
    CMAKE_FLAGS+='-DOPEN_TP=ON '
else
    CMAKE_FLAGS+='-DOPEN_TP=OFF '
fi

if [[ "$BUILD_UT" == 'ON' ]]; then
    CMAKE_FLAGS+="-DDEBUG_UT=ON "
else
    CMAKE_FLAGS+="-DDEBUG_UT=OFF "
fi

if [[ "$PROMETHEUS_FLAG" == 'ON' ]]; then
    CMAKE_FLAGS+="-DOPEN_PROMETHEUS=ON "
else
    CMAKE_FLAGS+="-DOPEN_PROMETHEUS=OFF "
fi

cd ${PROJ_DIR}
if [[ ! -d ${PROJ_DIR}/3rdparty/ubs-comm/ubs-comm ]]; then
    echo "Trying to git clone ubs-comm ..."
    cd ${PROJ_DIR}/3rdparty/ubs-comm
    git clone https://atomgit.com/openeuler/ubs-comm.git
    cd ${PROJ_DIR}/3rdparty/ubs-comm/ubs-comm
    git checkout br_noncom_container_20260228 && git submodule update --init
fi

if [[ ! -d ${PROJ_DIR}/3rdparty/libboundscheck/libboundscheck ]]; then
    echo "Trying to git clone libboundscheck ..."
    cd ${PROJ_DIR}/3rdparty/libboundscheck
    git clone https://gitee.com/openeuler/libboundscheck.git
    cd ${PROJ_DIR}/3rdparty/libboundscheck/libboundscheck
    git checkout v1.1.16
fi


if [[ -z "${CI_BUILD}" ]];then
    echo "update submodules ... "
    cd $PROJ_DIR && git submodule update --init
    cd $PROJ_DIR/3rdparty/prometheus/prometheus && git submodule update --init
fi

CPU_PROCESSOR_NUM=$(($(grep processor /proc/cpuinfo | wc -l) -2))
CMAKE_CMD="cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE $CMAKE_FLAGS $PROJ_DIR"
BUILD_CMD="make install -j ${CPU_PROCESSOR_NUM}"
cd $BUILD_DIR
echo $CMAKE_CMD
$CMAKE_CMD || {
    echo "Failed to configure boostio build."
	  exit 1
}
echo $BUILD_CMD
$BUILD_CMD || {
	  echo "Failed to build boostio."
	  exit 1
}
cd ${PROJ_DIR}/dist
if [[ "$BUILD_TYPE" == "debug" ]];then
	  \cp -d 3rdparty/zookeeper/lib/* bio/lib/.
	  \cp 3rdparty/libboundscheck/lib/* bio/lib/.
	  \cp 3rdparty/prometheus/lib64/*.so* bio/lib/.
fi

arch=$(uname -m)
if [[ "$arch" == "aarch64" ]] || [[ "$arch" == arm* ]]; then
  \cp 3rdparty/openssl/lib/*.so bio/lib/.
else
  \cp 3rdparty/openssl/lib64/*.so bio/lib/.
fi

\cp -d 3rdparty/ubs-comm/lib/libhcom.so* bio/lib/.
\cp ${PROJ_DIR}/3rdparty/ubs-comm/ubs-comm/dist/hcom_3rdparty/libboundscheck/lib/libboundscheck.so bio/lib/.
\cp 3rdparty/ubs-comm/include/hcom/*.h bio/include/.
\cp 3rdparty/ubs-comm/include/hcom/capi/*.h bio/include/.

if [[ "$BUILD_TYPE" == "release" && "$CLI_FLAG" == "ON" ]]; then
    mkdir -p test_tools
    mkdir -p test_tools/bin
    mkdir -p test_tools/lib
    mkdir -p test_tools/conf
    mv bio/bin/cli_server test_tools/bin/.
    mv bio/bin/cli_client test_tools/bin/.
    mv bio/bin/bio_console test_tools/bin/.
    mv bio/lib/libcli_agent.so test_tools/lib/.
    mv bio/lib/libsdk_diagnose.so test_tools/lib/.
    mv bio/lib/libserver_diagnose.so test_tools/lib/.
    \cp ../configs/bio_sdk_test.conf test_tools/conf/.
    \cp -d 3rdparty/zookeeper/lib/* test_tools/lib/.
    rm -rf bio/lib/libcli_agent.a
    tar -czvf BoostIO_$(uname -s)-$(arch)_test_tools.tar.gz test_tools
fi

chmod 550 -R ../scripts/*
\cp -r ../scripts bio/.
touch bio/scripts/host_ip_list
rm -rf boostio
mv bio boostio
tar -czvf BoostIO_1.0.0_$(uname -s)-$(arch)_${BUILD_TYPE}.tar.gz boostio


