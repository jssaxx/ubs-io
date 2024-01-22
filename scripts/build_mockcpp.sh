#!/usr/bin/env bash
# Copyright: (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
set -xe

PROJECT_HOME="$( cd "$( dirname "$0" )"/.. && pwd  )"
BUILD_PATH=${PROJECT_HOME}/test/3rdparty/build
OUTPUT_PATH=${PROJECT_HOME}/output/3rdparty/mockcpp

if [ ! -d "${BUILD_PATH}" ]; then
  mkdir -p ${BUILD_PATH}
fi

if [ ! -d "${OUTPUT_PATH}" ]; then
  mkdir -p ${OUTPUT_PATH}
fi

cd ${BUILD_PATH}
cmake ${PROJECT_HOME}/test/3rdparty/mockcpp -DCMAKE_INSTALL_PREFIX:STRING=${OUTPUT_PATH}
make -j && make install