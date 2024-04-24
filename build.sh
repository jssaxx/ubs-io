#!/bin/bash
# Copyright: (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
set -xe
 
CURRENT_PATH="$(dirname "${BASH_SOURCE[0]}")"
PROJ_DIR="$(realpath "${CURRENT_PATH}")"
BUILD_DIR=${PROJ_DIR}/build
BUILD_TYPE=debug
DIAGNOSE_FLAG=true
DIAGNOSE=on
CMAKE_FLAGS=
arch=$(uname -m)
if [ ! -d "${BUILD_DIR}" ]; then
  mkdir -p ${BUILD_DIR}
fi

if [[ "$1" == 'Debug' ]]; then
  BUILD_TYPE=debug
  CMAKE_FLAGS+='-DOPEN_TEST_TP_TOOLS=ON '
elif [[ "$1" == 'Release' ]]; then
  BUILD_TYPE=release
  CMAKE_FLAGS+='-DOPEN_TEST_TP_TOOLS=OFF '
fi

if [[ "$2" == 'Ut' ]]; then
    BUILD_UT=ON
    CMAKE_FLAGS+="-DDEBUG_UT=ON "
else
	BUILD_UT=OFF
	CMAKE_FLAGS+="-DDEBUG_UT=OFF "
fi

if [[ "$3" == 'Ceph' ]];then
  CMAKE_FLAGS+='-DOPEN_UNDERFS_CEPH=ON '
fi

cd $BUILD_DIR
echo "BUILD_DIR=${BUILD_DIR}"

CMAKE_FLAGS+='-DOPEN_TEST_TOOLS=ON '

CPU_PROCESSOR_NUM=$(($(grep processor /proc/cpuinfo | wc -l) -2))
CMAKE_CMD="cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE $CMAKE_FLAGS $PROJ_DIR"
BUILD_CMD="make install -j ${CPU_PROCESSOR_NUM}"

echo $CMAKE_CMD
$CMAKE_CMD || {
    echo "Failed to configure boostio build."
    exit -1
}
echo
echo $BUILD_CMD
$BUILD_CMD || {
    echo "Failed to build boostio."
    exit -1
}
echo
echo "build boostio successful."

cd ${PROJ_DIR}/output
rm -rf boostio
mkdir -p boostio/lib
mkdir -p boostio/bin
mkdir -p boostio/include
mkdir -p boostio/scripts
mkdir -p boostio/conf
\cp 3rdparty/zookeeper/lib/* boostio/lib/.
\cp 3rdparty/spdlog/lib64/libspdlog.a boostio/lib/.
\cp 3rdparty/hcom/lib/*.so boostio/lib/.
\cp 3rdparty/hcom/lib/securec/* boostio/lib/.
\cp 3rdparty/zookeeper/lib/* boostio/lib/.
\cp 3rdparty/huawei_secure_c/lib/* boostio/lib/.
\cp 3rdparty/io_interceptor/lib/libock_interceptor.so boostio/lib/.
\cp bio/conf/*.conf boostio/conf/.
\cp bio/lib/libbio_sdk.so boostio/lib/.
\cp bio/lib/libbio_server.so boostio/lib/.
\cp bio/lib/libsecurec.so boostio/lib/.
\cp bio/lib/libbio_interceptor_server.so boostio/lib/.
\cp bio/lib/libock_iofwd_proxy.so boostio/lib/.

echo "当前cpu架构：$arch"
if [[ "$DIAGNOSE_FLAG $arch" == 'true aarch64' ]]; then
    \cp bio/lib/libtracepoint.a boostio/lib/.
    \cp bio/lib/libtracepoint.so boostio/lib/.
fi

if [[ "$DIAGNOSE_FLAG" == 'true' ]]; then
    \cp bio/lib/libcli_agent.a boostio/lib/.
    \cp bio/lib/libcli_agent.so boostio/lib/.
    \cp bio/lib/*_diagnose.so boostio/lib/.
    \cp bio/lib/libbio_tracepoint.so boostio/lib/.
fi

\cp bio/bin/* boostio/bin/.
\cp ../scripts/* boostio/scripts/.
\cp bio/include/*.h boostio/include/.
cd boostio/scripts
touch host_ip_list
cd ${PROJ_DIR}/output
tar -czvf BoostIO_1.0.0_$(uname -s)-`arch`_${BUILD_TYPE}.tar.gz boostio
