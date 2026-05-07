
#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# script for building and packaging UBSIO-KV and UBSIO-Boostio
# version: 1.0.0
# ***********************************************************************

set -e

BUILD_TYPE="release"
ZK_BUILD_DIR="/tmp/zookeeper-build"

CURRENT_PATH="$(dirname "${BASH_SOURCE[0]}")"
PROJ_DIR="$(realpath "${CURRENT_PATH}/..")"
KV_DIR="${PROJ_DIR}/ubsio-kv"
BOOSTIO_DIR="${PROJ_DIR}/ubsio-boostio"
OUTPUT_DIR="${PROJ_DIR}/dist"

if [ -f "${PROJ_DIR}/VERSION" ]; then
    VERSION=$(cat "${PROJ_DIR}/VERSION")
else
    VERSION="0.0.1"
fi

TARBALL_NAME="ubsio-kv-${VERSION}.tar.gz"

echo "========================================================"
echo "Building and Packaging UBSIO-KV"
echo "========================================================"
echo "Version:       ${VERSION}"
echo "Build Type:    ${BUILD_TYPE}"
echo "Project Dir:   ${PROJ_DIR}"
echo "Output Dir:    ${OUTPUT_DIR}"
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
    if ! command -v python3 &> /dev/null; then
        missing="${missing} python3"
    fi
    if ! command -v pip3 &> /dev/null; then
        missing="${missing} pip3"
    fi
    if ! command -v make &> /dev/null; then
        missing="${missing} make"
    fi

    if [ -n "$missing" ]; then
        echo "  Error: Missing dependencies:${missing}"
        echo "  Please install dependencies first:"
        echo "    yum install -y cmake gcc gcc-c++ python3 python3-pip make"
        exit 1
    fi

    echo "  All dependencies found."
}

check_source_code() {
    echo "[1/4] Checking source code..."

    if [ ! -d "${KV_DIR}" ]; then
        echo "  Error: ubsio-kv directory not found: ${KV_DIR}"
        exit 1
    fi

    if [ ! -d "${BOOSTIO_DIR}" ]; then
        echo "  Error: ubsio-boostio directory not found: ${BOOSTIO_DIR}"
        exit 1
    fi

    if [ ! -d "${ZK_BUILD_DIR}/output/lib" ]; then
        echo "  Error: Zookeeper not built. Please run build_zookeeper.sh first."
        exit 1
    fi

    echo "  Source code check passed."
}

build_ubsio_kv() {
    echo "[2/4] Building UBSIO-KV (includes ubsio-boostio)..."

    cd "${KV_DIR}"
    bash build.sh -t ${BUILD_TYPE}

    if [ $? -ne 0 ]; then
        echo "  Error: Failed to build UBSIO-KV"
        exit 1
    fi

    echo "  UBSIO-KV built successfully."
}

package_libraries() {
    echo "[3/4] Packaging libraries..."

    mkdir -p "${OUTPUT_DIR}/ubsio/lib"
    mkdir -p "${OUTPUT_DIR}/ubsio/bin"
    mkdir -p "${OUTPUT_DIR}/ubsio/conf"
    mkdir -p "${OUTPUT_DIR}/zookeeper/lib"

    echo "  Packaging UBSIO-KV..."
    if [ -d "${KV_DIR}/dist/lib" ]; then
        cp -r "${KV_DIR}/dist/lib/"* "${OUTPUT_DIR}/ubsio/lib/"
    fi
    if [ -d "${PROJ_DIR}/ubsio-nds/lib" ]; then
        cp -r "${PROJ_DIR}/ubsio-nds/lib/"* "${OUTPUT_DIR}/ubsio/lib/"
    fi

    echo "  Packaging UBSIO-Boostio..."
    if [ -d "${BOOSTIO_DIR}/dist/boostio/lib" ]; then
        cp -r "${BOOSTIO_DIR}/dist/boostio/lib/"* "${OUTPUT_DIR}/ubsio/lib/"
    fi
    if [ -d "${BOOSTIO_DIR}/dist/3rdparty/ubs-comm/lib" ]; then
        cp -r "${BOOSTIO_DIR}/dist/3rdparty/ubs-comm/lib/"* "${OUTPUT_DIR}/ubsio/lib/"
    fi
    if [ -d "${BOOSTIO_DIR}/dist/boostio/bin" ]; then
        cp -r "${BOOSTIO_DIR}/dist/boostio/bin/"* "${OUTPUT_DIR}/ubsio/bin/"
    fi
    if [ -f "${BOOSTIO_DIR}/dist/boostio/conf/bio.conf" ]; then
        cp "${BOOSTIO_DIR}/dist/boostio/conf/bio.conf" "${OUTPUT_DIR}/ubsio/conf/"
    fi

    echo "  Packaging Zookeeper Client..."
    if [ -d "${ZK_BUILD_DIR}/output/lib" ]; then
        cp -r "${ZK_BUILD_DIR}/output/lib/"* "${OUTPUT_DIR}/zookeeper/lib/"
    fi

    echo "  Libraries packaged successfully."
}

create_scripts_and_tarball() {
    echo "[4/4] Copying installation scripts and creating tarball..."

    cp "${CURRENT_PATH}/install.sh" "${OUTPUT_DIR}/install.sh"
    cp "${CURRENT_PATH}/uninstall.sh" "${OUTPUT_DIR}/uninstall.sh"

    chmod +x "${OUTPUT_DIR}/install.sh"
    chmod +x "${OUTPUT_DIR}/uninstall.sh"

    echo "  Installation scripts copied."

    echo "  Creating tarball..."
    cd "${OUTPUT_DIR}"
    tar -czf "${TARBALL_NAME}" ubsio/ zookeeper/ install.sh uninstall.sh

    echo "  Tarball created: ${OUTPUT_DIR}/${TARBALL_NAME}"

    echo "  Cleaning up..."
    rm -rf "${OUTPUT_DIR}/ubsio"
    rm -rf "${OUTPUT_DIR}/zookeeper"
    rm -f "${OUTPUT_DIR}/install.sh"
    rm -f "${OUTPUT_DIR}/uninstall.sh"

    echo "  Cleanup completed."
}

main() {
    check_dependencies
    check_source_code
    build_ubsio_kv
    package_libraries
    create_scripts_and_tarball

    echo ""
    echo "========================================================"
    echo "UBSIO-KV packaging completed."
    echo "========================================================"
    echo "Output: ${OUTPUT_DIR}/${TARBALL_NAME}"
    echo ""
    echo "Usage:"
    echo "  1. Copy ${TARBALL_NAME} to target environment"
    echo "  2. Extract: tar -xzvf ${TARBALL_NAME}"
    echo "  3. Install: bash install.sh"
    echo "  4. Uninstall: bash uninstall.sh"
}

main "$@"
