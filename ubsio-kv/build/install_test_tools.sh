#!/bin/bash
# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

set -euo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly KV_ROOT="$(realpath "${SCRIPT_DIR}/..")"
readonly BOOSTIO_INSTALLER="${KV_ROOT}/../ubsio-boostio/build/install_test_tools.sh"

if [[ -z "${KV_TEST_TOOL_PATH:-}" ]]; then
    KV_TEST_TOOL_PATH="${KV_ROOT}/dist/kv_test_tools"
fi

if [[ ! -f "${BOOSTIO_INSTALLER}" ]]; then
    echo "BoostIO test tool installer not found: ${BOOSTIO_INSTALLER}" >&2
    exit 1
fi

echo "[install_test_tools.sh] KV_TEST_TOOL_PATH: ${KV_TEST_TOOL_PATH}"
if [[ -f "${KV_TEST_TOOL_PATH}/googletest/include/gtest/gtest.h" ]]; then
    echo "[install_test_tools.sh] googletest is already installed."
    exit 0
fi

# Reuse the pinned GoogleTest version and download/build flow maintained by BoostIO.
BOOSTIO_TEST_TOOL_PATH="${KV_TEST_TOOL_PATH}" \
    bash "${BOOSTIO_INSTALLER}"
