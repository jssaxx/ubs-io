#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# script for Huawei ubs-io to build pkg
# version: 1.0.0
# ***********************************************************************

set -e
usage() {
    echo "Usage: $0 [ -h | -help ] [ -t | -type <build_type> ] [--ut=UT] [--cli=Diagnose] [--tp=tracepoint] [--pms=prometheus] [--san=asan] [--build_kv <ON|OFF>] [--pkg]"
    echo "build_type: [debug, release, clean]"
    echo "Examples:"
    echo " 1 ./build.sh -t release // 禁止添加tp功能, 对外发布包禁止添加cli功能"
    echo " 2 ./build.sh -t debug // 默认添加cli和tp功能"
    echo " 3 ./build.sh -t debug [--ut] // 限制仅DT构建脚本使用"
    echo " 4 ./build.sh -t debug --san=asan // Build BoostIO and test tools with ASan+UBSan"
    echo " 5 ./build.sh -t release --build_kv OFF // Skip UBSIO-KV packaging"
    echo " 6 ./build.sh -t release --pkg // Copy built libraries to dist/lib"
    echo
    exit 1;
}

copy_named_libs_from_dir() {
    local src_dir=$1
    local dst_dir=$2
    shift 2
    local lib_name

    [[ -d "${src_dir}" ]] || return 0
    for lib_name in "$@"; do
        [[ -e "${src_dir}/${lib_name}" || -L "${src_dir}/${lib_name}" ]] || continue
        cp -a "${src_dir}/${lib_name}" "${dst_dir}/"
    done
}

package_libs() {
    local pkg_lib_dir="${PROJ_DIR}/dist/lib"

    echo "Packaging libraries to ${pkg_lib_dir}"
    rm -rf "${pkg_lib_dir}"
    mkdir -p "${pkg_lib_dir}"

    copy_named_libs_from_dir "${PROJ_DIR}/dist/boostio/lib" "${pkg_lib_dir}" \
        libbio_common.so \
        libbio_interceptor_server.so \
        libbio_sdk.a \
        libbio_sdk.so \
        libbio_sdk.so.1 \
        libbio_sdk.so.1.0.0 \
        libbio_security.so \
        libbio_server.so \
        libbio_tracepoint.so \
        libbio_underfs.so \
        libhtracer.so \
        libock_interceptor.so \
        libock_iofwd_proxy.so \
        libtracepoint.so

    if [[ "$BUILD_KV" == "ON" ]]; then
        copy_named_libs_from_dir "${PROJ_DIR}/../ubsio-kv/dist/lib" "${pkg_lib_dir}" \
            libubsio_kvc.so \
            libubsio_kvc.so.1 \
            libubsio_kvc.so.1.0.0
    fi

    copy_named_libs_from_dir "${PROJ_DIR}/dist/3rdparty/libboundscheck/lib" "${pkg_lib_dir}" \
        libboundscheck.so
    copy_named_libs_from_dir "${PROJ_DIR}/dist/3rdparty/libaio/lib" "${pkg_lib_dir}" \
        libaio.so \
        libaio.so.1 \
        libaio.so.1.0.2

    if [[ "$BUILD_TYPE" == "debug" ]]; then
        copy_named_libs_from_dir "${PROJ_DIR}/dist/test_tools/lib" "${pkg_lib_dir}" \
            libcli_agent.so \
            libsdk_diagnose.so \
            libserver_diagnose.so
    fi
}

CURRENT_PATH="$(dirname "${BASH_SOURCE[0]}")"
PROJ_DIR="$(realpath "${CURRENT_PATH}")"
BUILD_DIR=${PROJ_DIR}/Build
BUILD_UT=OFF
TP_FLAG=OFF
CLI_FLAG=OFF
PROMETHEUS_FLAG=OFF
SANITIZER_FLAG=OFF
BUILD_KV=ON
BUILD_PKG=OFF
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
            TP_FLAG=ON
            CLI_FLAG=ON
            shift ;;
        --cli )
            CLI_FLAG=ON
            shift ;;
        --tp )
            TP_FLAG=ON
            shift ;;
        --pms )
            PROMETHEUS_FLAG=ON
            shift ;;
        --build_kv )
            kv_flag="$2"
            kv_flag=${kv_flag^^}
            [[ "$kv_flag" != "ON" && "$kv_flag" != "OFF" ]] && echo "Invalid build_kv flag $2" && usage
            BUILD_KV=$kv_flag
            shift 2 ;;
        --pkg )
            BUILD_PKG=ON
            shift
            ;;
        --san=asan | --san=asan-ubsan | --asan | --asan-ubsan )
            SANITIZER_FLAG=ON
            shift ;;
        --san )
            [[ "$2" != "asan" && "$2" != "asan-ubsan" ]] && echo "Invalid sanitizer $2" && usage
            SANITIZER_FLAG=ON
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

if [[ "$TP_FLAG $arch" == 'ON aarch64' ]]; then
    CMAKE_FLAGS+='-DOPEN_TP=ON '
else
    CMAKE_FLAGS+='-DOPEN_TP=OFF '
fi

if [[ "$CLI_FLAG" == 'ON' ]]; then
    CMAKE_FLAGS+="-DOPEN_CLI=ON "
else
    CMAKE_FLAGS+="-DOPEN_CLI=OFF "
fi

if [[ "$BUILD_UT" == 'ON' ]]; then
    [[ -z "${TEST_TOOL_PATH}" ]] && TEST_TOOL_PATH="${PROJ_DIR}/dist/boostio_test_tools"
    if [[ ! -d "${TEST_TOOL_PATH}" ]]; then
        echo "boostio test tools are not installed, installing..."
        bash "${PROJ_DIR}/build/install_test_tools.sh"
    fi

    CMAKE_FLAGS+="-DDEBUG_UT=ON "
    CMAKE_FLAGS+="-DTEST_TOOL_INSTALL_PATH=${TEST_TOOL_PATH} "
else
    CMAKE_FLAGS+="-DDEBUG_UT=OFF "
fi

if [[ "$PROMETHEUS_FLAG" == 'ON' ]]; then
    CMAKE_FLAGS+="-DOPEN_PROMETHEUS=ON "
else
    CMAKE_FLAGS+="-DOPEN_PROMETHEUS=OFF "
fi

if [[ "$SANITIZER_FLAG" == 'ON' ]]; then
    CMAKE_FLAGS+="-DBOOSTIO_ENABLE_ASAN_UBSAN=ON "
else
    CMAKE_FLAGS+="-DBOOSTIO_ENABLE_ASAN_UBSAN=OFF "
fi

CPU_PROCESSOR_NUM=$(($(grep processor /proc/cpuinfo | wc -l) -2)) # CI环境核数会波动, 个人使用时用这个变量
CMAKE_CMD="cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE $CMAKE_FLAGS $PROJ_DIR"
BUILD_CMD="make install -j 16" # CI环境核数会波动, 默认只用16
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
if [[ "$PROMETHEUS_FLAG" == 'ON' ]];then
	  \cp 3rdparty/prometheus/lib64/*.so* bio/lib/.
fi


if [[ "$CLI_FLAG" == "ON" && "$BUILD_UT" != "ON" ]]; then
    cd ${PROJ_DIR}/../ubsio-common/cli
    CLI_BUILD_FLAGS=""
    if [[ "$SANITIZER_FLAG" == "ON" ]]; then
        CLI_BUILD_FLAGS+="--san=asan"
    fi
    bash build.sh ${CLI_BUILD_FLAGS}
    cd ${PROJ_DIR}/dist
    mkdir -p test_tools
    mkdir -p test_tools/bin
    mkdir -p test_tools/lib
    mkdir -p test_tools/conf
    mv bio/bin/bio_console test_tools/bin/.
    mv bio/lib/libsdk_diagnose.so test_tools/lib/.
    mv bio/lib/libserver_diagnose.so test_tools/lib/.
    mv ${PROJ_DIR}/../ubsio-common/cli/Build/src/libcli_agent.so test_tools/lib/.
    mv ${PROJ_DIR}/../ubsio-common/cli/Build/src/cli_server test_tools/bin/.
    mv ${PROJ_DIR}/../ubsio-common/cli/Build/src/cli_client test_tools/bin/.
    \cp ../configs/bio_sdk_test.conf test_tools/conf/.
    tar -czvf BoostIO_$(uname -s)-$(arch)_test_tools.tar.gz test_tools
fi

rm -rf boostio
mv bio boostio
if [[ "$BUILD_KV" == "ON" ]]; then
    mkdir -p ${PROJ_DIR}/dist/boostio/kv
    mkdir -p ${PROJ_DIR}/dist/boostio/kv/lib
    mkdir -p ${PROJ_DIR}/dist/boostio/kv/include
    mkdir -p ${PROJ_DIR}/dist/boostio/kv/pkg
    cd ${PROJ_DIR}/../ubsio-kv/
    bash build.sh -t ${BUILD_TYPE}
    cd ${PROJ_DIR}/dist
    \cp ${PROJ_DIR}/../ubsio-kv/dist/lib/* ${PROJ_DIR}/dist/boostio/kv/lib/.
    \cp ${PROJ_DIR}/../ubsio-kv/dist/include/* ${PROJ_DIR}/dist/boostio/kv/include/.
    \cp ${PROJ_DIR}/../ubsio-kv/dist/pkg/* ${PROJ_DIR}/dist/boostio/kv/pkg/.
fi

if [[ "$BUILD_PKG" == "ON" ]]; then
    package_libs
fi

tar -czvf BoostIO_1.0.0_$(uname -s)-$(arch)_${BUILD_TYPE}.tar.gz boostio
