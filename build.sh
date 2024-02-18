#!/bin/bash
# Copyright: (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
set -xe
 
CURRENT_PATH="$(dirname "${BASH_SOURCE[0]}")"
PROJ_DIR="$(realpath "${CURRENT_PATH}")"
BUILD_DIR=${PROJ_DIR}/build
BUILD_TYPE=debug
 
if [ ! -d "${BUILD_DIR}" ]; then
  mkdir -p ${BUILD_DIR}
fi
 
if [[ "$1" == 'Debug' ]]; then
  BUILD_TYPE=debug
elif [[ "$1" == 'Release' ]]; then
  BUILD_TYPE=release
fi
 
CI_BUILD=$2
if [ -z "${CI_BUILD}" ];then
    cd $PROJ_DIR && git submodule update --init
    cd $PROJ_DIR/3rdparty/hcom/hcom && git submodule update --init
fi

# build mockcpp for ut
cd $PROJ_DIR
sh scripts/build_mockcpp.sh

cd $BUILD_DIR
echo "BUILD_DIR=${BUILD_DIR}"
 
CMAKE_CMD="cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE $PROJ_DIR"
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

if [ -n "${RUN_DT}" ];then
    cd ${BUILD_DIR}
    ./test/bio_test --gtest_output=xml:./

    lcov -d ./ -c -o all.info --rc lcov_branch_coverage=1
    lcov -r all.info '*opt/buildtools*' '*/3rdparty/*' -o new.info --rc lcov_branch_coverage=1
    genhtml -o result new.info --rc genhtml_branch_coverage=1
    mv bio_test.xml result/test_detail.xml

    cd result
    zip -r lcov.zip *
    artget pull "ock_3rdparty ock3rdparty1.0" -ru software -user p_OckCI -pwd \
    encryption:ETMsDgAAAYgIefwyABFBRVMvR0NNL05vUGFkZGluZwCAABAAEBKGslaG2E1RnzCAiRGoekcAAAAqIwJz1WwrhJUvE4ohzMKYYtHPTBeTa7LlILcfVZJoOuQOYEmRgSMNt85UABQBhk4+/kX90aleLjjXzrA/G5tcGw== -rp "hdfsutil.jar" -ap "./"
    java -jar hdfsutil.jar -prod -upload lcov.zip ${upload_path}/lcov.zip
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
\cp bio/bin/* boostio/bin/.
\cp ../scripts/* boostio/scripts/.
\cp bio/include/*.h boostio/include/.
cd boostio/scripts
touch host_ip_list
cd ${PROJ_DIR}/output
tar -czvf BoostIO_1.0.0_$(uname -s)-`arch`_${BUILD_TYPE}.tar.gz boostio
