#!/usr/bin/env bash
# 功能：
# 1. 读取本地写脚本生成的所有 case 文件
# 2. 覆盖 read / pread / pread64 / readv / preadv64
# 3. 覆盖小中大以及大于 4MiB 的读取大小
# 4. 按确定性数据模式校验远端读取结果和写入内容一致
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

echo "执行远端读取与数据一致性校验..."
if [[ -n "${INTERCEPTOR_SO:-}" ]]; then
  LD_PRELOAD="${INTERCEPTOR_SO}" "${BIN}" read "$1"
else
  "${BIN}" read "$1"
fi
