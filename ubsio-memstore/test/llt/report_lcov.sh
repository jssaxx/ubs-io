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

make_cov_info()
{
    SUB_DIR=$1
    SUB_GENERATE_DIR=${CURRENT_DIR}/cov/gen/${SUB_DIR}
    rm -rf ${SUB_GENERATE_DIR}; mkdir -p ${SUB_GENERATE_DIR}

    cd ${PROJECT_HOME}/build

    find ${PROJECT_HOME}/build/${SUB_DIR} -name "*.gcda" | xargs -r -i cp {} ${SUB_GENERATE_DIR}
    find ${PROJECT_HOME}/build/${SUB_DIR} -name "*.gcno" | xargs -r -i cp {} ${SUB_GENERATE_DIR}

    if ! find ${SUB_GENERATE_DIR} -name "*.gcda" | grep -q .; then
        return 0
    fi

    # generate all coverage
    tmp_file="coverage.info"
    lcov --d ${SUB_GENERATE_DIR} --c --output-file ${SUB_GENERATE_DIR}/${tmp_file} --rc lcov_branch_coverage=1
    if [ 0 != $? ];then
    echo "Failed to generate all coverage info"
    exit 1
    fi
}

#分目录并行收集gcda，提升效率
for i in "src/cache" "src/client" "src/cluster" "src/common" "src/config" "../ubsio-common/tracer" "src/memory" "src/net" "src/server" "test/llt" "test/tools"
do
    { make_cov_info $i
    }&
done
wait

INFO_FILES=$(find ${GENERATE_DIR} -name "coverage.info" | sort)
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
