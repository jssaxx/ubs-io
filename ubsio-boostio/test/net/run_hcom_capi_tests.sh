#!/bin/bash
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
# Licensed under the Mulan PSL v2.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
OUT="${ROOT}/Build/hcom_capi_tests"
mkdir -p "${OUT}"
INCLUDES=(-I"${ROOT}/src/net" -I"${ROOT}/src/common" -I"${ROOT}/dist/3rdparty/ubs-comm/include")
"${CXX:-g++}" -std=c++11 -pthread "${INCLUDES[@]}" \
    "${ROOT}/test/net/hcom_capi_adapter_test.cpp" -o "${OUT}/adapter_test"
"${OUT}/adapter_test"
"${CXX:-g++}" -std=c++11 -pthread "${INCLUDES[@]}" \
    "${ROOT}/test/net/hcom_capi_smoke.cpp" "${ROOT}/src/net/hcom_api_dl.cpp" \
    -ldl -o "${OUT}/smoke"
export LD_LIBRARY_PATH="${ROOT}/dist/3rdparty/ubs-comm/lib:${ROOT}/dist/3rdparty/libboundscheck/lib:${LD_LIBRARY_PATH:-}"
timeout 60 "${OUT}/smoke" tcp
timeout 60 "${OUT}/smoke" shm

# Production libraries must reach HCOM exclusively through the C symbol table.
for library in "${ROOT}/dist/boostio/lib/libbio_sdk.so" "${ROOT}/dist/boostio/lib/libbio_server.so"; do
    if readelf -d "${library}" | grep -q 'NEEDED.*libhcom'; then
        echo "Unexpected HCOM direct dependency in ${library}" >&2
        exit 1
    fi
    if nm -D --undefined-only --format=posix "${library}" | awk '{print $1}' | c++filt |
        grep -E '^(ock::hcom::|ubs_hcom_)'; then
        echo "Unexpected unresolved HCOM symbols in ${library}" >&2
        exit 1
    fi
done
echo 'HCOM dynamic-loading verification passed'
