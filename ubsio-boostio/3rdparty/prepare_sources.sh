#!/bin/bash
# Copyright: (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_DIR="$(realpath "${SCRIPT_DIR}/..")"
UBS_COMM_DIR="${PROJ_DIR}/3rdparty/ubs-comm/ubs-comm"
UBS_COMM_OFFLINE_DIR="${UBS_COMM_DIR}/3rdparty/offline"

fetch_repo() {
    local name=$1
    local repo=$2
    local ref=$3
    local dst=$4

    echo "Preparing ${name} at ${dst}"
    if [[ -d "${dst}/.git" ]]; then
        git -C "${dst}" fetch --tags origin
        git -C "${dst}" reset --hard HEAD
        git -C "${dst}" clean -fd
    elif [[ -e "${dst}" ]]; then
        if [[ -n "$(find "${dst}" -mindepth 1 -maxdepth 1 -print -quit)" ]]; then
            echo "Non-git directory already exists: ${dst}" >&2
            echo "Move it aside or convert it to the expected git checkout before retrying." >&2
            exit 1
        fi
        git clone "${repo}" "${dst}"
    else
        mkdir -p "$(dirname "${dst}")"
        git clone "${repo}" "${dst}"
    fi

    if git -C "${dst}" rev-parse --verify --quiet "refs/remotes/origin/${ref}" >/dev/null; then
        git -C "${dst}" checkout -B "${ref}" "origin/${ref}"
    else
        git -C "${dst}" checkout --detach "${ref}"
    fi
    git -C "${dst}" reset --hard HEAD
    git -C "${dst}" clean -fd
    git -C "${dst}" submodule update --init --recursive
}

apply_patch_once() {
    local name=$1
    local patch_file=$2
    local dst=$3

    echo "Applying ${name} patch at ${dst}"
    if git -C "${dst}" apply -p1 --check "${patch_file}"; then
        git -C "${dst}" apply -p1 "${patch_file}"
    elif git -C "${dst}" apply -p1 --reverse --check "${patch_file}"; then
        echo "  ${name} patch already applied."
    else
        echo "Failed to apply ${name} patch: ${patch_file}" >&2
        exit 1
    fi
}

fetch_repo "libboundscheck" "https://gitcode.com/openeuler/libboundscheck.git" \
    "v1.1.16" "${PROJ_DIR}/3rdparty/libboundscheck/libboundscheck"
fetch_repo "libaio" "https://github.com/deepin-community/libaio.git" \
    "0.3.113-8deepin2" "${PROJ_DIR}/3rdparty/libaio/libaio"
fetch_repo "spdlog" "https://gitcode.com/GitHub_Trending/sp/spdlog.git" \
    "v1.15.3" "${PROJ_DIR}/3rdparty/spdlog/spdlog"
# Current inference deployment does not enable prometheus. Keep this disabled
# to avoid fetching an unused dependency during offline source preparation.
# fetch_repo "prometheus-cpp" "https://gitcode.com/gh_mirrors/pr/prometheus-cpp.git" \
#     "v1.2.4" "${PROJ_DIR}/3rdparty/prometheus/prometheus"
fetch_repo "ubs-comm" "https://gitcode.com/openeuler/ubs-comm.git" \
    "tag_BeiMing-I26.0.RC1.B013" "${UBS_COMM_DIR}"

fetch_repo "ubs-comm libboundscheck" "https://gitcode.com/openeuler/libboundscheck.git" \
    "v1.1.16" "${UBS_COMM_OFFLINE_DIR}/libboundscheck"
fetch_repo "ubs-comm umdk" "https://atomgit.com/openeuler/umdk.git" \
    "br_openEuler_24.03_LTS_SP3" "${UBS_COMM_OFFLINE_DIR}/umdk"
apply_patch_once "ubs-comm offline internal dependencies" \
    "${SCRIPT_DIR}/patches/ubs-comm-offline-internal-deps.patch" "${UBS_COMM_DIR}"

echo "All third-party sources are ready under ${PROJ_DIR}/3rdparty."
