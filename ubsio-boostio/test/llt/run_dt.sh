#!/bin/bash
set -e

readonly SCRIPT_FULL_PATH=$(dirname $(readlink -f "$0"))
readonly PROJECT_ROOT=$(cd "${SCRIPT_FULL_PATH}/../../"; pwd)
readonly BUILD_DIR="${PROJECT_ROOT}/Build"
readonly REPORT_DIR="${PROJECT_ROOT}/coverage_report"

# 临时文件
readonly BASELINE_INFO="${REPORT_DIR}/baseline.info"
readonly TEST_INFO="${REPORT_DIR}/test.info"
readonly TOTAL_INFO="${REPORT_DIR}/total.info"
readonly FINAL_INFO="${REPORT_DIR}/coverage.info"

echo "=== 1. Aggressive Cleaning (Force Rebuild) ==="
# 强制删除 build 目录，确保 CMake 配置生效且所有文件重新编译
if [ -d "${BUILD_DIR}" ]; then
    rm -rf "${BUILD_DIR}"
fi
rm -rf ${REPORT_DIR}
mkdir -p ${REPORT_DIR}

echo "=== 2. Building Project ==="
bash ${PROJECT_ROOT}/build.sh -t debug --ut

echo "=== 3. Capturing Baseline (Initial State) ==="
# 扫描所有生成的 .gcno 文件，将所有源文件标记为 0% 覆盖
lcov --capture --initial \
     --directory ${PROJECT_ROOT}/Build \
     --output-file ${BASELINE_INFO} \
     --rc lcov_branch_coverage=1 \
     --quiet

echo "=== 4. Running Tests ==="
cd ${BUILD_DIR}
\cp -r ${PROJECT_ROOT}/dist/boostio/lib/* ${BUILD_DIR}/
export LD_LIBRARY_PATH=${BUILD_DIR}:$LD_LIBRARY_PATH
./bio_test --gtest_output="xml:${REPORT_DIR}/report.xml"

echo "=== 5. Capturing Test Data (Execution State) ==="
cd ${PROJECT_ROOT}
# 扫描 .gcda 文件，获取实际执行情况
lcov --capture \
     --directory ${PROJECT_ROOT}/Build \
     --output-file ${TEST_INFO} \
     --rc lcov_branch_coverage=1 \
     --quiet

echo "=== 6. Merging & Filtering ==="
# 合并基准数据和测试数据
lcov --add-tracefile ${BASELINE_INFO} \
     --add-tracefile ${TEST_INFO} \
     --output-file ${TOTAL_INFO} \
     --rc lcov_branch_coverage=1 \
     --quiet

# 过滤，去除 python 库干扰
lcov --remove ${TOTAL_INFO} \
     "*/llt/*" \
     "/usr/*" \
     "/opt/*" \
     "*/test/*" \
     "*7.3.0*" \
     "*/3rdparty/*" \
     "*/googletest/*" \
     "*/Build/*" \
     "*/interceptor/client/*" \
     "*/io_interceptor/*" \
     "*/security/openssl_tools/*" \
     "*/disk/common/ngx_rbtree.c" \
     "*/security/expiration_check/expire_checker.cpp" \
     "*/output/*" \
     "*/cmake-build-*" \
     --output-file ${FINAL_INFO} \
     --rc lcov_branch_coverage=1 \
     --quiet

echo "=== 7. Generating HTML Report ==="
# --prefix: 移除绝对路径前缀，让报告显示 cache/...
genhtml ${FINAL_INFO} \
        --output-directory ${REPORT_DIR} \
        --rc genhtml_branch_coverage=1 \
        --prefix ${PROJECT_ROOT}/src \
        --ignore-errors source

echo "SUCCESS! Report generated at: ${REPORT_DIR}/index.html"