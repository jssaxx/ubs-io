#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# script for Huawei ubs-io to build pkg
# version: 1.0.0
# ***********************************************************************
set -e

readonly CURRENT_SCRIPT_DIR=$(cd $(dirname ${0}) && pwd)
readonly BOOSTIO_ROOT_DIR=$(dirname ${CURRENT_SCRIPT_DIR})
readonly BOOSTIO_LOG_TAG="[$(basename ${0})]"
readonly TEST_TOOL_BUILD_DIR="${BOOSTIO_ROOT_DIR}/build/tmp_dir_for_prepare_test"

if [ -z "${BOOSTIO_TEST_TOOL_PATH}" ]; then
    echo "${BOOSTIO_LOG_TAG} BOOSTIO_TEST_TOOL_PATH is empty, set to default value."
    BOOSTIO_TEST_TOOL_PATH="${BOOSTIO_ROOT_DIR}/dist/boostio_test_tools"
fi
echo "${BOOSTIO_LOG_TAG} BOOSTIO_TEST_TOOL_PATH: ${BOOSTIO_TEST_TOOL_PATH}"
echo "${BOOSTIO_LOG_TAG} TEST_TOOL_BUILD_DIR: ${TEST_TOOL_BUILD_DIR}"

GTEST_INSTALL_PATH="${BOOSTIO_TEST_TOOL_PATH}/googletest"
MOCKCPP_INSTALL_PATH="${BOOSTIO_TEST_TOOL_PATH}/mockcpp"
echo "${BOOSTIO_LOG_TAG} GTEST_INSTALL_PATH: ${GTEST_INSTALL_PATH}"
echo "${BOOSTIO_LOG_TAG} MOCKCPP_INSTALL_PATH: ${MOCKCPP_INSTALL_PATH}"

# prepare test tool build dir
if [ -d "${TEST_TOOL_BUILD_DIR}" ]; then
    rm -rf ${TEST_TOOL_BUILD_DIR}
fi
mkdir -p ${TEST_TOOL_BUILD_DIR}

# prepare googletest
cd ${TEST_TOOL_BUILD_DIR}
git clone https://codehub.devcloud.cn-north-4.huaweicloud.com/aca5f619a7a34d3fb99b76a842fda236/googletest.git
cd googletest
git checkout -b release-1.12.1 release-1.12.1
mkdir build && cd build
cmake -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=${GTEST_INSTALL_PATH} -DINSTALL_GTEST=ON ..
make -j8
make install
echo "${BOOSTIO_LOG_TAG} googletest install to ${GTEST_INSTALL_PATH} success."

# clean
rm -rf ${TEST_TOOL_BUILD_DIR}
