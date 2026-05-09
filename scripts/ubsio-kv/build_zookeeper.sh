
#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# script for building Zookeeper Client
# version: 1.0.0
# ***********************************************************************

set -e

ZK_VERSION="release-3.9.3"
BUILD_DIR="/tmp/zookeeper-build"
OUTPUT_DIR="${BUILD_DIR}/output"

echo "========================================================"
echo "Building Zookeeper Client"
echo "========================================================"
echo "Version:    ${ZK_VERSION}"
echo "Build Dir:  ${BUILD_DIR}"
echo "Output Dir: ${OUTPUT_DIR}"
echo ""

check_dependencies() {
    echo "[0/4] Checking dependencies..."

    local missing=""

    if ! command -v cmake &> /dev/null; then
        missing="${missing} cmake"
    fi
    if ! command -v gcc &> /dev/null; then
        missing="${missing} gcc"
    fi
    if ! command -v g++ &> /dev/null; then
        missing="${missing} g++"
    fi
    if ! command -v autoconf &> /dev/null; then
        missing="${missing} autoconf"
    fi
    if ! command -v automake &> /dev/null; then
        missing="${missing} automake"
    fi
    if ! command -v libtool &> /dev/null; then
        missing="${missing} libtool"
    fi
    if ! command -v mvn &> /dev/null; then
        missing="${missing} maven"
    fi

    if [ -n "$missing" ]; then
        echo "  Error: Missing dependencies:${missing}"
        echo "  Please install dependencies first:"
        echo "    yum install -y cmake gcc gcc-c++ autoconf automake libtool maven java-1.8.0-openjdk"
        exit 1
    fi

    echo "  All dependencies found."
}

clone_zookeeper() {
    echo "[1/4] Cloning Zookeeper source code..."

    if [ -d "${BUILD_DIR}/zookeeper" ]; then
        rm -rf "${BUILD_DIR}/zookeeper"
    fi

    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"

    git clone -b ${ZK_VERSION} https://gitcode.com/gh_mirrors/zo/zookeeper.git

    echo "  Zookeeper source code cloned."
}

build_zookeeper_jute() {
    echo "[2/4] Building Zookeeper Jute..."

    cd "${BUILD_DIR}/zookeeper/zookeeper-jute"
    mvn clean install -DskipTests

    echo "  Zookeeper Jute built successfully."
}

build_zookeeper_client() {
    echo "[3/4] Building Zookeeper Client..."

    cd "${BUILD_DIR}/zookeeper/zookeeper-client/zookeeper-client-c/"
    autoreconf -if

    mkdir -p "${OUTPUT_DIR}"
    ./configure --without-cppunit --prefix="${OUTPUT_DIR}"

    make -j$(nproc)

    echo "  Zookeeper Client built successfully."
}

package_zookeeper() {
    echo "[4/4] Packaging Zookeeper Client..."

    mkdir -p "${OUTPUT_DIR}/lib"

    cd "${BUILD_DIR}/zookeeper/zookeeper-client/zookeeper-client-c/"
    make install

    echo "  Zookeeper Client packaged successfully."
}

main() {
    check_dependencies
    clone_zookeeper
    build_zookeeper_jute
    build_zookeeper_client
    package_zookeeper

    echo ""
    echo "========================================================"
    echo "Zookeeper Client build completed."
    echo "========================================================"
    echo "Output: ${OUTPUT_DIR}"
    echo ""
    echo "Note: Run build_ubsio.sh to create the final distribution package."
}

main "$@"
