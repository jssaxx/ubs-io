#!/bin/bash
# Copyright: (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
set -xe
 
CURRENT_PATH="$(dirname "${BASH_SOURCE[0]}")"
PROJ_DIR="$(realpath "${CURRENT_PATH}")"
BUILD_DIR=${PROJ_DIR}/build
BUILD_TYPE=debug
DIAGNOSE_FLAG=false
DIAGNOSE=off
CMAKE_FLAGS=
arch=$(uname -m)
if [ ! -d "${BUILD_DIR}" ]; then
  mkdir -p ${BUILD_DIR}
fi

if [[ "$1" == 'Debug' ]]; then
  BUILD_TYPE=debug
elif [[ "$1" == 'Release' ]]; then
  BUILD_TYPE=release
fi

if [[ "$2" == 'Ut' ]]; then
    BUILD_UT=ON
    CMAKE_FLAGS+="-DDEBUG_UT=ON "
fi

if [ -n "${BUILD_UT}" ];then
    # build mockcpp for ut
    cd $PROJ_DIR
    sh scripts/build_mockcpp.sh
fi

if [[ "$3" == 'diagnose' ]];then
  DIAGNOSE=on
fi

cd $BUILD_DIR
echo "BUILD_DIR=${BUILD_DIR}"

if [[ "$BUILD_TYPE" == 'debug' ]]; then
    CMAKE_FLAGS+='-DOPEN_TEST_TOOLS=ON '
    DIAGNOSE_FLAG=true
fi

if [[ "$DIAGNOSE_FLAG $DIAGNOSE" == 'false on' ]]; then
    CMAKE_FLAGS+='-DOPEN_TEST_TOOLS=ON '
    DIAGNOSE_FLAG=true
fi
CMAKE_CMD="cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE $CMAKE_FLAGS $PROJ_DIR"
BUILD_CMD="make install -j 8"

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

if [ -n "${BUILD_UT}" ];then
    cd ${BUILD_DIR}/test/llt
    ./bio_test --gtest_output=xml:./

    lcov -d ../../src -c -o all.info --rc lcov_branch_coverage=1
    lcov -r all.info '/usr/include/*' '*/3rdparty/*' -o new.info --rc lcov_branch_coverage=1
	genhtml new.info --output-directory result
	tar cvf result.tar result
fi

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
\cp bio/conf/*.conf boostio/conf/.
\cp bio/lib/libbio_sdk.so boostio/lib/.
\cp bio/lib/libbio_server.so boostio/lib/.
\cp bio/lib/libsecurec.so boostio/lib/.

echo "当前cpu架构：$arch"
if [[ "$BUILD_TYPE $arch" == 'debug aarch64' ]]; then
    \cp bio/lib/libtracepoint.a boostio/lib/.
    \cp bio/lib/libtracepoint.so boostio/lib/.
fi

if [[ "$DIAGNOSE_FLAG" == 'true' ]]; then
    \cp bio/lib/libcli_agent.a boostio/lib/.
    \cp bio/lib/libcli_agent.so boostio/lib/.
    \cp bio/lib/*_diagnose.so boostio/lib/.
fi

\cp bio/bin/* boostio/bin/.
\cp ../scripts/* boostio/scripts/.
\cp bio/include/*.h boostio/include/.
cd boostio/scripts
touch host_ip_list
cd ${PROJ_DIR}/output
tar -czvf BoostIO_1.0.0_$(uname -s)-`arch`_${BUILD_TYPE}.tar.gz boostio
