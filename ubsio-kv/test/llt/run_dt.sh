#!/bin/bash
# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

set -euo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly PROJECT_ROOT="$(realpath "${SCRIPT_DIR}/../..")"
readonly BUILD_DIR="${PROJECT_ROOT}/Build"
readonly REPORT_DIR="${PROJECT_ROOT}/coverage_report"
readonly BASELINE_INFO="${REPORT_DIR}/baseline.info"
readonly TEST_INFO="${REPORT_DIR}/test.info"
readonly TOTAL_INFO="${REPORT_DIR}/total.info"
readonly FINAL_INFO="${REPORT_DIR}/coverage.info"
readonly MIN_LINE_COVERAGE="85"
readonly MIN_BRANCH_COVERAGE="60"

for tool in cmake g++; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "Required tool not found: ${tool}" >&2
        exit 1
    fi
done

USE_GCOV=0
if ! command -v lcov >/dev/null 2>&1 || ! command -v genhtml >/dev/null 2>&1; then
    USE_GCOV=1
    for tool in gcov python3; do
        if ! command -v "${tool}" >/dev/null 2>&1; then
            echo "Required coverage tool not found: ${tool}" >&2
            exit 1
        fi
    done
fi

echo "=== 1. Clean and build ubsio-kv UT ==="
rm -rf "${BUILD_DIR}" "${REPORT_DIR}"
mkdir -p "${REPORT_DIR}"
bash "${PROJECT_ROOT}/build.sh" -t debug --ut --build_python OFF

export LD_LIBRARY_PATH="${BUILD_DIR}:${BUILD_DIR}/fake_ascend/lib64:${LD_LIBRARY_PATH:-}"
export ASCEND_HOME_PATH="${BUILD_DIR}/fake_ascend"
export UBSIO_KV_UT_ROOT="${PROJECT_ROOT}"

LCOV_RC_OPTS=(
    --rc lcov_branch_coverage=1
    # GCC maps exception-only iostream and logging implementation branches to source lines.
    # Loader error behavior is covered separately by kv_loader_test.
    --rc 'lcov_excl_br_line=LCOV_EXCL_BR_LINE|DL_LOAD_SYM|LOG_.*|UBSIO_KVC_ASSERT_.*|std::cout'
)

echo "=== 2. Capture coverage baseline ==="
if [[ "${USE_GCOV}" -eq 0 ]]; then
    lcov --capture --initial \
        --directory "${BUILD_DIR}" \
        --output-file "${BASELINE_INFO}" \
        "${LCOV_RC_OPTS[@]}" \
        --quiet
fi

echo "=== 3. Run unit tests ==="
cd "${BUILD_DIR}"
LD_LIBRARY_PATH="${BUILD_DIR}/incomplete:${BUILD_DIR}/src/csrc" \
ASCEND_HOME_PATH="${BUILD_DIR}/incomplete_ascend" \
./kv_loader_test --gtest_output="xml:${REPORT_DIR}/loader-report.xml"

./kv_test --gtest_output="xml:${REPORT_DIR}/report.xml"

if [[ "${USE_GCOV}" -eq 1 ]]; then
    echo "=== 4. Summarize GCC coverage ==="
    python3 "${PROJECT_ROOT}/../test/tools/gcov_summary.py" \
        --project kv --root "${PROJECT_ROOT}" --build "${BUILD_DIR}" \
        --report "${REPORT_DIR}" --min-line "${MIN_LINE_COVERAGE}" \
        --min-branch "${MIN_BRANCH_COVERAGE}"
    exit 0
fi

echo "=== 4. Capture and filter coverage ==="
lcov --capture \
    --directory "${BUILD_DIR}" \
    --output-file "${TEST_INFO}" \
    "${LCOV_RC_OPTS[@]}" \
    --quiet

lcov --add-tracefile "${BASELINE_INFO}" \
    --add-tracefile "${TEST_INFO}" \
    --output-file "${TOTAL_INFO}" \
    --rc lcov_branch_coverage=1 \
    --quiet

# Header-only utilities under utils are production code and must remain in coverage.
lcov --extract "${TOTAL_INFO}" \
    '*/ubsio-kv/src/csrc/*/*.cpp' \
    '*/ubsio-kv/src/csrc/utils/*.h' \
    --output-file "${FINAL_INFO}" \
    "${LCOV_RC_OPTS[@]}" \
    --quiet

genhtml "${FINAL_INFO}" \
    --output-directory "${REPORT_DIR}" \
    --rc genhtml_branch_coverage=1 \
    --prefix "${PROJECT_ROOT}/src" \
    --ignore-errors source \
    --quiet

summary="$(lcov --summary "${FINAL_INFO}" --rc lcov_branch_coverage=1 2>&1)"
echo "${summary}"
line_coverage="$(awk '/lines\.*:/ {gsub("%", "", $2); print $2}' <<<"${summary}")"
branch_coverage="$(awk '/branches\.*:/ {gsub("%", "", $2); print $2}' <<<"${summary}")"
if [[ -z "${line_coverage}" ]]; then
    echo "Unable to parse line coverage." >&2
    exit 1
fi
if ! awk -v actual="${line_coverage}" -v minimum="${MIN_LINE_COVERAGE}" \
    'BEGIN { exit !(actual > minimum) }'; then
    echo "Line coverage ${line_coverage}% does not exceed ${MIN_LINE_COVERAGE}%." >&2
    exit 1
fi

if [[ -z "${branch_coverage}" ]]; then
    echo "Unable to parse branch coverage." >&2
    exit 1
fi
if ! awk -v actual="${branch_coverage}" -v minimum="${MIN_BRANCH_COVERAGE}" \
    'BEGIN { exit !(actual > minimum) }'; then
    echo "Branch coverage ${branch_coverage}% does not exceed ${MIN_BRANCH_COVERAGE}%." >&2
    exit 1
fi

echo "SUCCESS: line coverage ${line_coverage}% exceeds ${MIN_LINE_COVERAGE}%; " \
    "branch coverage ${branch_coverage}% exceeds ${MIN_BRANCH_COVERAGE}%."
echo "HTML report: ${REPORT_DIR}/index.html"
