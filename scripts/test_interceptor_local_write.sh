#!/usr/bin/env bash
# 功能：
# 1. 覆盖 write / pwrite / pwrite64 / writev / pwritev / pwritev64
# 2. 覆盖小中大以及大于 4MiB 的写入大小
# 3. 每次写入后在本地立即读回，校验数据一致性
# 4. 大于 4MiB 的用例用于验证功能正确性和原生回退路径
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "用法: $0 <挂载目录>"
  exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${SCRIPT_DIR}/interceptor_io_matrix.c"
TMP_DIR="$(mktemp -d)"
BIN="${TMP_DIR}/interceptor_io_matrix"

cleanup() {
  rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

echo "编译测试程序..."
cc -O2 -Wall -Wextra -std=c11 "${SRC}" -o "${BIN}"

echo "执行本地写入与本地一致性校验..."
if [[ -n "${INTERCEPTOR_SO:-}" ]]; then
  LD_PRELOAD="${INTERCEPTOR_SO}" "${BIN}" write "$1"
else
  "${BIN}" write "$1"
fi
