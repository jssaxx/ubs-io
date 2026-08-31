#!/bin/bash
#
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
#
# ubs-io is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#      http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
#

set -euo pipefail

CURRENT_PATH=$(cd "$(dirname "$0")"; pwd)
PROJECT_HOME=$(cd "${CURRENT_PATH}/../.."; pwd)
BUILD_DIR="${PROJECT_HOME}/build"
UT_RUN_DIR="${BUILD_DIR}/test/llt"
UT_CONF_DIR="${BUILD_DIR}/test/conf"
REPORT_DIR="${BUILD_DIR}/hdt_report"

asan="off"
if [[ "${1:-}" == 'asan' ]]; then
    asan="on"
    shift
fi

echo "enable asan: ${asan}"

cmake_args=(
    -DCMAKE_BUILD_TYPE=debug
    -DDEBUG_UT=ON
    -DOPEN_CLI=OFF
    -DOPEN_TP=OFF
)

if [[ "${asan}" == "on" ]]; then
    cmake_args+=(
        -DCMAKE_C_FLAGS=-fsanitize=address
        -DCMAKE_CXX_FLAGS=-fsanitize=address
        -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address
        -DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address
    )
fi

cmake -S "${PROJECT_HOME}" -B "${BUILD_DIR}" "${cmake_args[@]}"
cmake --build "${BUILD_DIR}" --target mms_test -j "$(nproc)"

# Clean stale gcov runtime data before executing UT to avoid timestamp conflicts.
find "${BUILD_DIR}" -name '*.gcda' -delete

rm -rf "${UT_CONF_DIR}"
mkdir -p "${UT_RUN_DIR}" "${UT_CONF_DIR}" "${REPORT_DIR}"
cp -r "${PROJECT_HOME}/conf/." "${UT_CONF_DIR}/"

cd "${UT_RUN_DIR}"
RUNTIME_LIB_DIRS=(
    "${BUILD_DIR}/src/client"
    "${BUILD_DIR}/src/server"
    "${PROJECT_HOME}/output/mms/lib"
    "${PROJECT_HOME}/output/3rdparty/libboundscheck/lib"
    "${PROJECT_HOME}/output/3rdparty/ubs-comm/lib"
)
export LD_LIBRARY_PATH="$(IFS=:; echo "${RUNTIME_LIB_DIRS[*]}"):${LD_LIBRARY_PATH:-}"
./mms_test --gtest_output=xml:report.xml "$@"
cp report.xml "${REPORT_DIR}/report.xml"

cd "${BUILD_DIR}"
rm -f "${PROJECT_HOME}/hdt.info"
rm -rf "${PROJECT_HOME}/hdt_report"
bash "${CURRENT_PATH}/report_lcov.sh"
