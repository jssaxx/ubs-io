#!/bin/bash
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
# Licensed under the Mulan PSL v2.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
# Load the package helpers without running the build entry point.
source <(sed '/^CURRENT_PATH=/,$d' "${ROOT}/build.sh")
TASK_DIR=$(mktemp -d)
trap 'rm -rf "${TASK_DIR}"' EXIT
PROJ_DIR="${TASK_DIR}/ubsio-boostio"
BUILD_KV=OFF
CLI_FLAG=OFF
SANITIZER_FLAG=OFF
mkdir -p "${PROJ_DIR}/dist/boostio/lib" "${PROJ_DIR}/dist/3rdparty/liburing/lib"

cat > "${TASK_DIR}/probe.c" <<'EOF'
int package_probe(void) { return 123; }
EOF
"${CC:-cc}" -g -shared -fPIC -Wl,-soname,libhcom.so.0 "${TASK_DIR}/probe.c" \
    -o "${PROJ_DIR}/dist/boostio/lib/libhcom.so.0.0.1"
ln -s libhcom.so.0.0.1 "${PROJ_DIR}/dist/boostio/lib/libhcom.so.0"
ln -s libhcom.so.0 "${PROJ_DIR}/dist/boostio/lib/libhcom.so"
cp "${PROJ_DIR}/dist/boostio/lib/libhcom.so.0.0.1" \
    "${PROJ_DIR}/dist/3rdparty/liburing/lib/liburing.so.2.6"
ln -s liburing.so.2.6 "${PROJ_DIR}/dist/3rdparty/liburing/lib/liburing.so.2"
ln -s liburing.so.2 "${PROJ_DIR}/dist/3rdparty/liburing/lib/liburing.so"
readelf -SW "${PROJ_DIR}/dist/boostio/lib/libhcom.so.0.0.1" > "${TASK_DIR}/original-sections.txt"
grep -q '\.debug_info' "${TASK_DIR}/original-sections.txt"
nm -D --defined-only "${PROJ_DIR}/dist/boostio/lib/libhcom.so.0.0.1" > "${TASK_DIR}/original-symbols.txt"

BUILD_TYPE=release
boostio_lib_dir="${PROJ_DIR}/dist/boostio/lib"
ln -s "${boostio_lib_dir}" "${TASK_DIR}/source-alias"
mkdir -p "${PROJ_DIR}/../ubsio-kv/dist/lib" "${PROJ_DIR}/dist/test_tools/lib" \
    "${PROJ_DIR}/dist/3rdparty/libboundscheck/lib"
for dst_dir in "${boostio_lib_dir}" "${TASK_DIR}/source-alias" "${PROJ_DIR}/dist/3rdparty/liburing/lib" \
        "${PROJ_DIR}/dist/3rdparty/libboundscheck/lib" "${PROJ_DIR}/../ubsio-kv/dist/lib" \
        "${PROJ_DIR}/dist/test_tools/lib"; do
    if BUILD_KV=ON CLI_FLAG=ON package_libs "${dst_dir}" > "${TASK_DIR}/overlap.log" 2>&1; then
        echo "Packaging into a source directory unexpectedly succeeded: ${dst_dir}" >&2
        exit 1
    fi
    grep -Fq 'is a package source directory' "${TASK_DIR}/overlap.log"
done
cmp "${boostio_lib_dir}/libhcom.so.0.0.1" "${PROJ_DIR}/dist/3rdparty/liburing/lib/liburing.so.2.6"
test "$(readlink "${boostio_lib_dir}/libhcom.so")" = libhcom.so.0
test "$(readlink "${boostio_lib_dir}/libhcom.so.0")" = libhcom.so.0.0.1

package_libs "${PROJ_DIR}/dist/lib"
test -f "${PROJ_DIR}/dist/lib/libhcom.so.0"
test ! -L "${PROJ_DIR}/dist/lib/libhcom.so.0"
test "$(find "${PROJ_DIR}/dist/lib" -maxdepth 1 -name 'libhcom.so*' | wc -l)" -eq 1
for library in libhcom.so.0 liburing.so.2.6; do
    readelf -SW "${PROJ_DIR}/dist/lib/${library}" > "${TASK_DIR}/packaged-sections.txt"
    if grep -q '\.debug_' "${TASK_DIR}/packaged-sections.txt"; then
        echo "Debug sections remain in release package: ${library}" >&2
        exit 1
    fi
done
test -L "${PROJ_DIR}/dist/lib/liburing.so"
nm -D --defined-only "${PROJ_DIR}/dist/lib/libhcom.so.0" > "${TASK_DIR}/packaged-symbols.txt"
cmp "${TASK_DIR}/original-symbols.txt" "${TASK_DIR}/packaged-symbols.txt"
python3 - "${PROJ_DIR}/dist/lib/libhcom.so.0" <<'PY'
import ctypes
import sys
assert ctypes.CDLL(sys.argv[1]).package_probe() == 123
PY

# Repackage a custom directory containing the old symlink layout and unrelated files.
CUSTOM_DIR="${TASK_DIR}/custom"
mkdir -p "${CUSTOM_DIR}"
cp -a "${PROJ_DIR}/dist/boostio/lib/"libhcom.so* "${CUSTOM_DIR}/"
cp "${boostio_lib_dir}/libhcom.so.0.0.1" "${CUSTOM_DIR}/libhcom.so.0.0.2"
ln -s libhcom.so.0.0.2 "${CUSTOM_DIR}/libhcom.so.1"
ln -s missing "${CUSTOM_DIR}/libhcom.so.9"
cp /bin/true "${boostio_lib_dir}/libhtracer.so"
ln -s libhcom.so.0 "${CUSTOM_DIR}/libhtracer.so"
printf 'keep\n' > "${CUSTOM_DIR}/unrelated.txt"
package_libs "${CUSTOM_DIR}"
test ! -L "${CUSTOM_DIR}/libhcom.so.0"
test "$(find "${CUSTOM_DIR}" -maxdepth 1 -name 'libhcom.so*' | wc -l)" -eq 1
grep -q '^keep$' "${CUSTOM_DIR}/unrelated.txt"
LD_LIBRARY_PATH="${CUSTOM_DIR}" python3 -c 'import ctypes; assert ctypes.CDLL("libhcom.so.0").package_probe() == 123'

BUILD_TYPE=debug
package_libs "${CUSTOM_DIR}"
readelf -SW "${CUSTOM_DIR}/libhcom.so.0" > "${TASK_DIR}/debug-sections.txt"
grep -q '\.debug_info' "${TASK_DIR}/debug-sections.txt"
BUILD_TYPE=release
SANITIZER_FLAG=ON
package_libs "${CUSTOM_DIR}"
readelf -SW "${CUSTOM_DIR}/libhcom.so.0" > "${TASK_DIR}/sanitizer-sections.txt"
grep -q '\.debug_info' "${TASK_DIR}/sanitizer-sections.txt"
readelf -SW "${PROJ_DIR}/dist/boostio/lib/libhcom.so.0.0.1" > "${TASK_DIR}/source-sections.txt"
grep -q '\.debug_info' "${TASK_DIR}/source-sections.txt"
test -L "${PROJ_DIR}/dist/boostio/lib/libhcom.so.0"

rm "${boostio_lib_dir}/libhcom.so.0"
for invalid_source in missing dangling text archive wrong_soname; do
    case "${invalid_source}" in
        dangling) ln -s missing "${boostio_lib_dir}/libhcom.so.0" ;;
        text) printf 'not ELF\n' > "${boostio_lib_dir}/libhcom.so.0" ;;
        archive) ar rcs "${boostio_lib_dir}/libhcom.so.0" "${boostio_lib_dir}/libhcom.so.0.0.1" ;;
        wrong_soname)
            "${CC:-cc}" -shared -fPIC -Wl,-soname,libhcom.so.1 "${TASK_DIR}/probe.c" -o "${boostio_lib_dir}/libhcom.so.0" ;;
    esac
    if package_libs "${CUSTOM_DIR}"; then
        echo "Packaging unexpectedly succeeded with ${invalid_source} HCOM source" >&2
        exit 1
    fi
    cmp "${CUSTOM_DIR}/libhcom.so.0" "${boostio_lib_dir}/libhcom.so.0.0.1"
    rm -f "${boostio_lib_dir}/libhcom.so.0"
done
echo 'Package library regression checks passed'
