#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# script for Huawei ubs-io to build pkg
# version: 1.0.0
# ***********************************************************************
set -e

usage() {
    echo "Usage: $0 [--san=asan]"
    echo "  --san=asan: Build CLI tools with ASan+UBSan"
    exit 1
}

CURRENT_PATH="$(dirname "${BASH_SOURCE[0]}")"
PROJ_DIR="$(realpath "${CURRENT_PATH}")"
BUILD_DIR=${PROJ_DIR}/Build
SANITIZER_FLAG=OFF

while true; do
    case "$1" in
        --san=asan | --san=asan-ubsan | --asan | --asan-ubsan )
            SANITIZER_FLAG=ON
            shift ;;
        --san )
            [[ "$2" != "asan" && "$2" != "asan-ubsan" ]] && echo "Invalid sanitizer $2" && usage
            SANITIZER_FLAG=ON
            shift 2 ;;
        -h | -help | --help )
            usage ;;
        * )
            break ;;
    esac
done

if [ ! -d "${BUILD_DIR}" ]; then
    mkdir -p ${BUILD_DIR}
fi
cd $BUILD_DIR
CFLAGS="-fPIC" CXXFLAGS="-fPIC" cmake -DCLI_ENABLE_ASAN_UBSAN=${SANITIZER_FLAG} ..
make -j
