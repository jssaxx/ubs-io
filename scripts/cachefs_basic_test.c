#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int join_path(char *buf, size_t cap, const char *base, const char *name)
{
    int n = snprintf(buf, cap, "%s/%s", base, name);
    if (n < 0 || (size_t)n >= cap) {
        fprintf(stderr, "[FAIL] 路径过长: %s/%s\n", base, name);
        return -1;
    }
    return 0;
}

static void fill_pattern(uint8_t *buf, size_t len, uint32_t seed)
{
    for (size_t i = 0; i < len; ++i) {
        buf[i] = (uint8_t)((seed + i * 17u) & 0xffu);
    }
}

static int check_file_size(const char *path, off_t expect)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "[FAIL] stat 失败 path=%s errno=%d\n", path, errno);
        return -1;
    }
    if (st.st_size != expect) {
        fprintf(stderr, "[FAIL] 文件大小错误 path=%s expect=%lld actual=%lld\n",
            path, (long long)expect, (long long)st.st_size);
        return -1;
    }
    return 0;
}

static int read_exact(const char *path, uint8_t *buf, size_t len)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "[FAIL] open 读失败 path=%s errno=%d\n", path, errno);
        return -1;
    }
    size_t done = 0;
    while (done < len) {
        ssize_t ret = read(fd, buf + done, len - done);
        if (ret < 0) {
            fprintf(stderr, "[FAIL] read 失败 path=%s errno=%d\n", path, errno);
            close(fd);
            return -1;
        }
        if (ret == 0) {
            break;
        }
        done += (size_t)ret;
    }
    close(fd);
    if (done != len) {
        fprintf(stderr, "[FAIL] read 长度不足 path=%s expect=%zu actual=%zu\n", path, len, done);
        return -1;
    }
    return 0;
}

static int expect_content(const char *path, const uint8_t *expect, size_t len)
{
    uint8_t *buf = (uint8_t *)malloc(len ? len : 1);
    if (buf == NULL) {
        fprintf(stderr, "[FAIL] malloc 失败 len=%zu\n", len);
        return -1;
    }
    int rc = read_exact(path, buf, len);
    if (rc == 0 && memcmp(buf, expect, len) != 0) {
        fprintf(stderr, "[FAIL] 数据不一致 path=%s len=%zu\n", path, len);
        rc = -1;
    }
    free(buf);
    return rc;
}

static int ensure_dir_exists(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "[FAIL] 目录不存在 path=%s errno=%d\n", path, errno);
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "[FAIL] 不是目录 path=%s\n", path);
        return -1;
    }
    return 0;
}

static int dir_contains(const char *dir_path, const char *name)
{
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        fprintf(stderr, "[FAIL] opendir 失败 path=%s errno=%d\n", dir_path, errno);
        return -1;
    }
    int found = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, name) == 0) {
            found = 1;
            break;
        }
    }
    closedir(dir);
    return found;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "用法: %s <挂载目录>\n", argv[0]);
        return 2;
    }

    const char *root = argv[1];
    char base[4096];
    char subdir[4096];
    char file1[4096];
    char file2[4096];
    char nested[4096];

    if (join_path(base, sizeof(base), root, "cachefs_basic_case") != 0 ||
        join_path(subdir, sizeof(subdir), base, "subdir") != 0 ||
        join_path(file1, sizeof(file1), subdir, "file_a.bin") != 0 ||
        join_path(file2, sizeof(file2), subdir, "file_b.bin") != 0 ||
        join_path(nested, sizeof(nested), subdir, "nested") != 0) {
        return 1;
    }

    rmdir(nested);
    unlink(file2);
    unlink(file1);
    rmdir(subdir);
    rmdir(base);

    if (mkdir(base, 0755) != 0) {
        fprintf(stderr, "[FAIL] mkdir 失败 path=%s errno=%d\n", base, errno);
        return 1;
    }
    if (mkdir(subdir, 0755) != 0) {
        fprintf(stderr, "[FAIL] mkdir 失败 path=%s errno=%d\n", subdir, errno);
        return 1;
    }
    if (mkdir(nested, 0755) != 0) {
        fprintf(stderr, "[FAIL] mkdir 失败 path=%s errno=%d\n", nested, errno);
        return 1;
    }
    if (ensure_dir_exists(base) != 0 || ensure_dir_exists(subdir) != 0 || ensure_dir_exists(nested) != 0) {
        return 1;
    }
    if (dir_contains(base, "subdir") != 1 || dir_contains(subdir, "nested") != 1) {
        fprintf(stderr, "[FAIL] readdir 校验失败\n");
        return 1;
    }

    uint8_t part1[4096];
    uint8_t part2[2048];
    uint8_t append[3000];
    fill_pattern(part1, sizeof(part1), 11);
    fill_pattern(part2, sizeof(part2), 29);
    fill_pattern(append, sizeof(append), 53);

    int fd = open(file1, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd < 0) {
        fprintf(stderr, "[FAIL] open 写失败 path=%s errno=%d\n", file1, errno);
        return 1;
    }

    if (write(fd, part1, sizeof(part1)) != (ssize_t)sizeof(part1)) {
        fprintf(stderr, "[FAIL] write 失败 errno=%d\n", errno);
        close(fd);
        return 1;
    }
    if (pwrite(fd, part2, sizeof(part2), 1024) != (ssize_t)sizeof(part2)) {
        fprintf(stderr, "[FAIL] pwrite 失败 errno=%d\n", errno);
        close(fd);
        return 1;
    }
    if (lseek(fd, 0, SEEK_END) < 0) {
        fprintf(stderr, "[FAIL] lseek 失败 errno=%d\n", errno);
        close(fd);
        return 1;
    }
    if (write(fd, append, sizeof(append)) != (ssize_t)sizeof(append)) {
        fprintf(stderr, "[FAIL] append write 失败 errno=%d\n", errno);
        close(fd);
        return 1;
    }

    off_t expect_size = (off_t)sizeof(part1) + (off_t)sizeof(append);
    if (ftruncate(fd, expect_size - 512) != 0) {
        fprintf(stderr, "[FAIL] ftruncate 失败 errno=%d\n", errno);
        close(fd);
        return 1;
    }
    expect_size -= 512;

    if (fsync(fd) != 0) {
        fprintf(stderr, "[FAIL] fsync 失败 errno=%d\n", errno);
        close(fd);
        return 1;
    }
    close(fd);

    uint8_t *expect = (uint8_t *)malloc((size_t)expect_size);
    if (expect == NULL) {
        fprintf(stderr, "[FAIL] malloc expect 失败\n");
        return 1;
    }
    memset(expect, 0, (size_t)expect_size);
    if ((size_t)expect_size >= sizeof(part1)) {
        memcpy(expect, part1, sizeof(part1));
    } else {
        memcpy(expect, part1, (size_t)expect_size);
    }
    if ((size_t)expect_size > 1024) {
        size_t part2_len = (size_t)expect_size - 1024;
        if (part2_len > sizeof(part2)) {
            part2_len = sizeof(part2);
        }
        memcpy(expect + 1024, part2, part2_len);
    }
    if ((size_t)expect_size > sizeof(part1)) {
        size_t append_len = (size_t)expect_size - sizeof(part1);
        if (append_len > sizeof(append)) {
            append_len = sizeof(append);
        }
        memcpy(expect + sizeof(part1), append, append_len);
    }

    if (check_file_size(file1, expect_size) != 0) {
        free(expect);
        return 1;
    }
    if (expect_content(file1, expect, (size_t)expect_size) != 0) {
        free(expect);
        return 1;
    }

    if (rename(file1, file2) != 0) {
        fprintf(stderr, "[FAIL] rename 失败 errno=%d\n", errno);
        free(expect);
        return 1;
    }
    if (access(file1, F_OK) == 0) {
        fprintf(stderr, "[FAIL] rename 后旧文件仍存在 path=%s\n", file1);
        free(expect);
        return 1;
    }
    if (check_file_size(file2, expect_size) != 0) {
        free(expect);
        return 1;
    }
    if (expect_content(file2, expect, (size_t)expect_size) != 0) {
        free(expect);
        return 1;
    }

    int fd2 = open(file2, O_RDWR);
    if (fd2 < 0) {
        fprintf(stderr, "[FAIL] open rename 后文件失败 errno=%d\n", errno);
        free(expect);
        return 1;
    }
    if (truncate(file2, 2048) != 0) {
        fprintf(stderr, "[FAIL] truncate 失败 errno=%d\n", errno);
        close(fd2);
        free(expect);
        return 1;
    }
    close(fd2);
    if (check_file_size(file2, 2048) != 0) {
        free(expect);
        return 1;
    }
    if (expect_content(file2, expect, 2048) != 0) {
        free(expect);
        return 1;
    }

    free(expect);

    if (unlink(file2) != 0) {
        fprintf(stderr, "[FAIL] unlink 失败 errno=%d\n", errno);
        return 1;
    }
    if (access(file2, F_OK) == 0) {
        fprintf(stderr, "[FAIL] unlink 后文件仍存在 path=%s\n", file2);
        return 1;
    }
    if (rmdir(nested) != 0 || rmdir(subdir) != 0 || rmdir(base) != 0) {
        fprintf(stderr, "[FAIL] rmdir 清理失败 errno=%d\n", errno);
        return 1;
    }

    printf("[PASS] cachefs 基本功能校验通过\n");
    return 0;
}
