#!/bin/bash
# Copyright: (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.

usage() {
    echo "Usage: $0 [ -h | -help ] [ -t | -type <build_type> ] [--cli=Diagnose] [--ut=UT] [--tp=tracepoint]"
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
BUILD_DIR=${PROJ_DIR}/build
BUILD_UT=OFF
CLI_FLAG=OFF
TP_FLAG=OFF
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
            [[ $type != "debug" && $type != "release" && $type != "clean" ]] && echo "Invalid build type $2" && usage
            if [[ $type == 'debug' ]]; then
                BUILD_TYPE=debug
			          TP_FLAG=ON
			          CLI_FLAG=ON
            elif [[ $type == 'release' ]]; then
                BUILD_TYPE=release
            elif [[ $type == 'clean' ]]; then
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
		    -h | -help )
            usage
            exit 0
            ;;
        * )
            break;;
    esac
done

if [ "$BUILD_TYPE" == "clean" ]; then
    cd $BUILD_DIR
    BUILD_CMD="make clean"
    echo
    echo $BUILD_CMD
    $BUILD_CMD || {
        echo "Failed to clean boostio."
        exit -1
    }
    echo
    echo "clean boostio successful."
    rm -rf ${PROJ_DIR}/output
    exit 0
fi

if [ "$BUILD_TYPE" == "release" ]; then
    TP_FLAG=OFF
    CMAKE_FLAGS+='-DOPEN_RELEASE=ON '
else
    CMAKE_FLAGS+='-DOPEN_RELEASE=OFF '
fi

if [ "$CLI_FLAG" == "ON" ]; then
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

CPU_PROCESSOR_NUM=$(($(grep processor /proc/cpuinfo | wc -l) -2))
CMAKE_CMD="cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE $CMAKE_FLAGS $PROJ_DIR"
BUILD_CMD="make install -j ${CPU_PROCESSOR_NUM}"
cd $BUILD_DIR
echo $CMAKE_CMD
$CMAKE_CMD || {
    echo "Failed to configure boostio build."
	  exit -1
}
echo $BUILD_CMD
$BUILD_CMD || {
	  echo "Failed to build boostio."
	  exit -1
}
cd ${PROJ_DIR}/output
if [[ "$BUILD_TYPE" == "debug" ]];then
	  \cp 3rdparty/zookeeper/lib/* bio/lib/.
	  \cp 3rdparty/spdlog/lib64/libspdlog.a bio/lib/.
	  \cp 3rdparty/hcom/lib/securec/* bio/lib/.
	  \cp 3rdparty/huawei_secure_c/lib/* bio/lib/.
fi
\cp 3rdparty/hseceasy/hse/cryption_tool/bin/* bio/bin/.
\cp 3rdparty/hseceasy/hse/cryption/lib/* bio/lib/.
\cp 3rdparty/bdm/lib/libbdm.so bio/lib/.
\cp 3rdparty/hcom/include/hcom/*.h bio/include/.
\cp 3rdparty/hcom/include/hcom/go/*.go bio/include/.
\cp 3rdparty/bdm/include/bdm_core.h bio/include/.
\cp -r ../scripts bio/.
touch bio/scripts/host_ip_list
mv bio boostio
tar -czvf BoostIO_1.0.0_$(uname -s)-$(arch)_${BUILD_TYPE}.tar.gz boostio


