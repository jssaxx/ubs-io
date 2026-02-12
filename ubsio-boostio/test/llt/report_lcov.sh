#!/bin/bash
# Copyright: (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
CURRENT_DIR="$( cd "$( dirname "$0"  )" && pwd  )"
PROJECT_HOME="$( cd "$( dirname "$0" )"/../..  && pwd  )"
GENERATE_DIR=${CURRENT_DIR}/cov/gen
rm -rf ${CURRENT_DIR}/cov/; mkdir -p ${GENERATE_DIR}

make_cov_info()
{
    SUB_DIR=$1
    SUB_GENERATE_DIR=${CURRENT_DIR}/cov/gen/${SUB_DIR}
    rm -rf ${SUB_GENERATE_DIR}; mkdir -p ${SUB_GENERATE_DIR}

    cd ${PROJECT_HOME}/Build

    find ${PROJECT_HOME}/Build/${SUB_DIR} -name "*.gcda" | xargs  -i cp {} ${SUB_GENERATE_DIR}
    find ${PROJECT_HOME}/Build/${SUB_DIR} -name "*.gcno" | xargs  -i cp {} ${SUB_GENERATE_DIR}

    # generate all coverage
    tmp_file="coverage.info"
    lcov --d ${SUB_GENERATE_DIR} --c --output-file ${SUB_GENERATE_DIR}/${tmp_file} --rc lcov_branch_coverage=1 \
             --rc lcov_excl_br_line="LCOV_EXCL_BR_LINE|NET_LOG*|CLIENT_LOG*|LOG*|BIO_TP_START*|ChkTrue*"
    if [ 0 != $? ];then
    echo "Failed to generate all coverage info"
    exit 1
    fi
}

#分目录并行收集gcda，提升效率
for i in "src/cache" "src/cluster" "src/common" "src/config" "src/disk" "src/flow" "src/htracer" "src/interceptor" "src/net" "src/sdk" "src/server" "src/underfs" "test/llt" "test/tools"
do
    { make_cov_info $i
    }&
done
wait

#汇总所有的info
lcov -a ${GENERATE_DIR}/src/cache/coverage.info -a ${GENERATE_DIR}/src/cluster/coverage.info -a ${GENERATE_DIR}/src/common/coverage.info \
-a ${GENERATE_DIR}/src/config/coverage.info -a ${GENERATE_DIR}/src/disk/coverage.info -a ${GENERATE_DIR}/src/flow/coverage.info \
-a ${GENERATE_DIR}/src/htracer/coverage.info -a ${GENERATE_DIR}/src/interceptor/coverage.info \
-a ${GENERATE_DIR}/src/net/coverage.info -a ${GENERATE_DIR}/src/sdk/coverage.info \
-a ${GENERATE_DIR}/src/server/coverage.info -a ${GENERATE_DIR}/src/underfs/coverage.info \
-a ${GENERATE_DIR}/test/llt/coverage.info -a ${GENERATE_DIR}/test/tools/coverage.info \
-o hdt.info --rc lcov_branch_coverage=1

#删除不统计的目录、文件
lcov -r hdt.info "*/llt/*" "/usr/*"  "*/build/*" "*/Build/*" "*/test/*" "*7.3.0*" "*/3rdparty/*" "*/disk/common/ngx_rbtree.c" "*/cluster/*" -o hdt.info --rc lcov_branch_coverage=1

genhtml -o hdt_report hdt.info --rc genhtml_branch_coverage=1