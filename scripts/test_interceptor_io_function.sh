#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "用法: $0 <测试目录> [编译器]"
    echo "示例: $0 /bfs"
    echo "可选环境变量:"
    echo "  INTERCEPTOR_SO=/usr/lib64/libock_interceptor.so"
    exit 1
fi

TEST_DIR=$1
CC_BIN=${2:-cc}

if [[ ! -d "${TEST_DIR}" ]]; then
    echo "测试目录不存在: ${TEST_DIR}" >&2
    exit 1
fi

if [[ -n "${INTERCEPTOR_SO:-}" ]]; then
    export LD_PRELOAD="${INTERCEPTOR_SO}"
    echo "使用 LD_PRELOAD=${LD_PRELOAD}"
fi

WORKDIR=$(mktemp -d /tmp/interceptor-io-test.XXXXXX)
SRC_FILE="${WORKDIR}/test_interceptor_io_function.c"
BIN_FILE="${WORKDIR}/test_interceptor_io_function"

cleanup() {
    rm -rf "${WORKDIR}"
}
trap cleanup EXIT

cat > "${SRC_FILE}" <<'EOF'
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#if !defined(__linux__)
#define pread64  pread
#define pwrite64 pwrite
#define preadv64 preadv
#define pwritev64 pwritev
#endif

#define CHECK(cond, fmt, ...) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "[FAIL] " fmt "\n", ##__VA_ARGS__); \
            return -1; \
        } \
    } while (0)

static void fill_pattern(uint8_t *buf, size_t len, uint8_t seed)
{
    for (size_t i = 0; i < len; ++i) {
        buf[i] = (uint8_t)(seed + (i % 251));
    }
}

static int read_all(const char *path, uint8_t *buf, size_t len)
{
    int fd = open(path, O_RDONLY);
    CHECK(fd >= 0, "open file for read failed, errno=%d", errno);
    size_t off = 0;
    while (off < len) {
        ssize_t ret = read(fd, buf + off, len - off);
        CHECK(ret >= 0, "read all failed, ret=%zd errno=%d", ret, errno);
        CHECK(ret > 0, "unexpected EOF when reading all, off=%zu len=%zu", off, len);
        off += (size_t)ret;
    }
    close(fd);
    return 0;
}

static int verify_whole_file(const char *path, const uint8_t *expected, size_t len, const char *tag)
{
    uint8_t *actual = (uint8_t *)malloc(len);
    CHECK(actual != NULL, "malloc failed");
    memset(actual, 0, len);
    CHECK(read_all(path, actual, len) == 0, "read_all failed for %s", tag);
    if (memcmp(actual, expected, len) != 0) {
        for (size_t i = 0; i < len; ++i) {
            if (actual[i] != expected[i]) {
                fprintf(stderr, "[FAIL] %s mismatch at offset %zu expect=%u actual=%u\n",
                    tag, i, (unsigned)expected[i], (unsigned)actual[i]);
                free(actual);
                return -1;
            }
        }
    }
    free(actual);
    printf("[PASS] %s\n", tag);
    return 0;
}

static int test_positioned_io(const char *dir)
{
    const size_t file_len = 32768;
    char path[4096];
    snprintf(path, sizeof(path), "%s/positional_io.bin", dir);

    uint8_t *expected = (uint8_t *)calloc(1, file_len);
    uint8_t *tmp = (uint8_t *)malloc(file_len);
    uint8_t *a = (uint8_t *)malloc(4096);
    uint8_t *b = (uint8_t *)malloc(4096);
    uint8_t *c = (uint8_t *)malloc(5000);
    uint8_t *d = (uint8_t *)malloc(6000);
    CHECK(expected && tmp && a && b && c && d, "malloc failed");

    fill_pattern(a, 4096, 0x11);
    fill_pattern(b, 4096, 0x22);
    fill_pattern(c, 5000, 0x33);
    fill_pattern(d, 6000, 0x44);

    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0644);
    CHECK(fd >= 0, "open positional file failed errno=%d", errno);
    CHECK(ftruncate(fd, (off_t)file_len) == 0, "ftruncate failed errno=%d", errno);

    ssize_t ret = pwrite(fd, a, 4096, 0);
    CHECK(ret == 4096, "pwrite failed ret=%zd errno=%d", ret, errno);
    memcpy(expected + 0, a, 4096);

    ret = pwrite64(fd, b, 4096, 8192);
    CHECK(ret == 4096, "pwrite64 failed ret=%zd errno=%d", ret, errno);
    memcpy(expected + 8192, b, 4096);

    struct iovec iov1[3];
    iov1[0].iov_base = c;
    iov1[0].iov_len = 1024;
    iov1[1].iov_base = c + 1024;
    iov1[1].iov_len = 2048;
    iov1[2].iov_base = c + 3072;
    iov1[2].iov_len = 1928;
    ret = pwritev(fd, iov1, 3, 16384);
    CHECK(ret == 5000, "pwritev failed ret=%zd errno=%d", ret, errno);
    memcpy(expected + 16384, c, 5000);

    struct iovec iov2[2];
    iov2[0].iov_base = d;
    iov2[0].iov_len = 2048;
    iov2[1].iov_base = d + 2048;
    iov2[1].iov_len = 3952;
    ret = pwritev64(fd, iov2, 2, 22000);
    CHECK(ret == 6000, "pwritev64 failed ret=%zd errno=%d", ret, errno);
    memcpy(expected + 22000, d, 6000);

    close(fd);
    CHECK(verify_whole_file(path, expected, file_len, "pwrite/pwrite64/pwritev/pwritev64 数据一致") == 0,
        "verify whole file failed");

    fd = open(path, O_RDONLY);
    CHECK(fd >= 0, "open positional file for read failed errno=%d", errno);

    memset(tmp, 0, file_len);
    ret = pread(fd, tmp, 4096, 0);
    CHECK(ret == 4096, "pread failed ret=%zd errno=%d", ret, errno);
    CHECK(memcmp(tmp, expected, 4096) == 0, "pread data mismatch");
    printf("[PASS] pread 数据一致\n");

    memset(tmp, 0, file_len);
    ret = pread64(fd, tmp, 4096, 8192);
    CHECK(ret == 4096, "pread64 failed ret=%zd errno=%d", ret, errno);
    CHECK(memcmp(tmp, expected + 8192, 4096) == 0, "pread64 data mismatch");
    printf("[PASS] pread64 数据一致\n");

    uint8_t *preadv_buf = (uint8_t *)malloc(5000);
    CHECK(preadv_buf != NULL, "malloc preadv_buf failed");
    struct iovec iov3[3];
    iov3[0].iov_base = preadv_buf;
    iov3[0].iov_len = 1200;
    iov3[1].iov_base = preadv_buf + 1200;
    iov3[1].iov_len = 1600;
    iov3[2].iov_base = preadv_buf + 2800;
    iov3[2].iov_len = 2200;
    ret = preadv64(fd, iov3, 3, 16384);
    CHECK(ret == 5000, "preadv64 failed ret=%zd errno=%d", ret, errno);
    CHECK(memcmp(preadv_buf, expected + 16384, 5000) == 0, "preadv64 data mismatch");
    printf("[PASS] preadv64 数据一致\n");
    free(preadv_buf);

    close(fd);
    free(expected);
    free(tmp);
    free(a);
    free(b);
    free(c);
    free(d);
    return 0;
}

static int test_sequential_io(const char *dir)
{
    const size_t len1 = 4096;
    const size_t len2 = 5000;
    const size_t file_len = len1 + len2;
    char path[4096];
    snprintf(path, sizeof(path), "%s/sequential_io.bin", dir);

    uint8_t *w1 = (uint8_t *)malloc(len1);
    uint8_t *w2 = (uint8_t *)malloc(len2);
    uint8_t *expected = (uint8_t *)malloc(file_len);
    uint8_t *read_back = (uint8_t *)malloc(file_len);
    CHECK(w1 && w2 && expected && read_back, "malloc failed");

    fill_pattern(w1, len1, 0x55);
    fill_pattern(w2, len2, 0x66);
    memcpy(expected, w1, len1);
    memcpy(expected + len1, w2, len2);

    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0644);
    CHECK(fd >= 0, "open sequential file failed errno=%d", errno);

    ssize_t ret = write(fd, w1, len1);
    CHECK(ret == (ssize_t)len1, "write failed ret=%zd errno=%d", ret, errno);

    struct iovec iov[3];
    iov[0].iov_base = w2;
    iov[0].iov_len = 1024;
    iov[1].iov_base = w2 + 1024;
    iov[1].iov_len = 2048;
    iov[2].iov_base = w2 + 3072;
    iov[2].iov_len = 1928;
    ret = writev(fd, iov, 3);
    CHECK(ret == (ssize_t)len2, "writev failed ret=%zd errno=%d", ret, errno);
    close(fd);

    CHECK(verify_whole_file(path, expected, file_len, "write/writev 数据一致") == 0,
        "verify sequential file failed");

    fd = open(path, O_RDONLY);
    CHECK(fd >= 0, "open sequential file for read failed errno=%d", errno);

    memset(read_back, 0, file_len);
    ret = read(fd, read_back, len1);
    CHECK(ret == (ssize_t)len1, "read failed ret=%zd errno=%d", ret, errno);
    CHECK(memcmp(read_back, expected, len1) == 0, "read data mismatch");
    printf("[PASS] read 数据一致\n");

    uint8_t *readv_buf = (uint8_t *)malloc(len2);
    CHECK(readv_buf != NULL, "malloc readv_buf failed");
    struct iovec riov[2];
    riov[0].iov_base = readv_buf;
    riov[0].iov_len = 2048;
    riov[1].iov_base = readv_buf + 2048;
    riov[1].iov_len = len2 - 2048;
    ret = readv(fd, riov, 2);
    CHECK(ret == (ssize_t)len2, "readv failed ret=%zd errno=%d", ret, errno);
    CHECK(memcmp(readv_buf, expected + len1, len2) == 0, "readv data mismatch");
    printf("[PASS] readv 数据一致\n");

    free(readv_buf);
    close(fd);
    free(w1);
    free(w2);
    free(expected);
    free(read_back);
    return 0;
}

int main(int argc, char **argv)
{
    CHECK(argc == 2, "usage: %s <test-dir>", argv[0]);
    CHECK(test_positioned_io(argv[1]) == 0, "positioned io test failed");
    CHECK(test_sequential_io(argv[1]) == 0, "sequential io test failed");
    printf("[PASS] 所有接口功能校验通过\n");
    return 0;
}
EOF

echo "编译测试程序..."
"${CC_BIN}" -D_GNU_SOURCE -O2 -Wall -Wextra -o "${BIN_FILE}" "${SRC_FILE}"

echo "执行功能校验..."
"${BIN_FILE}" "${TEST_DIR}"
