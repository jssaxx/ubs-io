#!/usr/bin/env bash
# 功能：
# 1. 验证缓存文件系统的基础语义功能
# 2. 覆盖 mkdir/rmdir/create/open/write/read/pwrite/append/fsync/ftruncate/truncate/rename/unlink/readdir/stat
# 3. 对关键写入内容做本地读回和字节级一致性校验
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "用法: $0 <挂载目录>"
  exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${SCRIPT_DIR}/cachefs_basic_test.c"
TMP_DIR="$(mktemp -d)"
BIN="${TMP_DIR}/cachefs_basic_test"

cleanup() {
  rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

echo "编译 cachefs 基本功能测试程序..."
cc -O2 -Wall -Wextra -std=c11 "${SRC}" -o "${BIN}"

echo "执行 cachefs 基本功能校验..."
if [[ -n "${INTERCEPTOR_SO:-}" ]]; then
  LD_PRELOAD="${INTERCEPTOR_SO}" "${BIN}" "$1"
else
  "${BIN}" "$1"
fi
