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

CURRENT_DIR="$( cd "$( dirname "$0"  )" && pwd  )"
PROJECT_HOME="$( cd "$( dirname "$0" )"/../..  && pwd  )"
GENERATE_DIR=${CURRENT_DIR}/cov/gen
rm -rf ${CURRENT_DIR}/cov/; mkdir -p ${GENERATE_DIR}

for tool in lcov genhtml
do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "Required command not found: ${tool}."
        exit 1
    fi
done

make_cov_info()
{
    SUB_DIR=$1
    SUB_GENERATE_DIR=${CURRENT_DIR}/cov/gen/${SUB_DIR}
    SUB_BUILD_DIR=${PROJECT_HOME}/build/${SUB_DIR}
    rm -rf ${SUB_GENERATE_DIR}; mkdir -p ${SUB_GENERATE_DIR}

    if [[ ! -d "${SUB_BUILD_DIR}" ]]; then
        return 0
    fi

    find "${SUB_BUILD_DIR}" -name "*.gcda" | xargs -r -i cp {} "${SUB_GENERATE_DIR}"
    find "${SUB_BUILD_DIR}" -name "*.gcno" | xargs -r -i cp {} "${SUB_GENERATE_DIR}"

    if ! find "${SUB_GENERATE_DIR}" -name "*.gcda" | grep -q .; then
        return 0
    fi

    # generate all coverage
    tmp_file="coverage.info"
    if ! lcov --d "${SUB_GENERATE_DIR}" --c --output-file "${SUB_GENERATE_DIR}/${tmp_file}" \
        --rc lcov_branch_coverage=1; then
        echo "Failed to generate coverage info, directory:${SUB_DIR}."
        return 1
    fi
}

#分目录并行收集gcda，提升效率
PIDS=()
for i in "src/cache" "src/client" "src/cluster" "src/common" "src/config" "ubsio-common/tracer" "src/memory" \
    "src/net" "src/server" "test/llt" "test/tools"
do
    { make_cov_info "$i"
    }&
    PIDS+=("$!")
done

COVERAGE_RESULT=0
for pid in "${PIDS[@]}"
do
    if ! wait "${pid}"; then
        COVERAGE_RESULT=1
    fi
done
if [[ "${COVERAGE_RESULT}" -ne 0 ]]; then
    exit 1
fi

INFO_FILES=$(find "${GENERATE_DIR}" -name "coverage.info" | sort)
if [[ -z "${INFO_FILES}" ]]; then
    echo "Failed to find coverage.info"
    exit 1
fi

LCOV_ARGS=()
for info in ${INFO_FILES}
do
    LCOV_ARGS+=(-a "${info}")
done

#汇总所有的info
lcov "${LCOV_ARGS[@]}" -o hdt.info --rc lcov_branch_coverage=1

#删除不统计的目录、文件
lcov -r hdt.info "*/llt/*" "/usr/*"  "*/build/*" "*/test/*" "*7.3.0*" "*/3rdparty/*" "*/client/*" "*/cluster/*" "*/net/*" "*/server/*" -o hdt.info --rc lcov_branch_coverage=1

genhtml -o hdt_report hdt.info --rc genhtml_branch_coverage=1
