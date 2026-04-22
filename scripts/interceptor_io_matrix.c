#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#if defined(__APPLE__)
typedef off_t off64_t;
#define pread64  pread
#define pwrite64 pwrite
#define preadv64 preadv
#define pwritev64 pwritev
#endif

struct write_case {
    const char *name;
    int id;
};

struct read_case {
    const char *name;
    int id;
};

static const struct write_case k_write_cases[] = {
    {"write", 1},
    {"pwrite", 2},
    {"pwrite64", 3},
    {"writev", 4},
    {"pwritev", 5},
    {"pwritev64", 6},
};

static const struct read_case k_read_cases[] = {
    {"read", 1},
    {"pread", 2},
    {"pread64", 3},
    {"readv", 4},
    {"preadv64", 5},
};

static const size_t k_sizes[] = {
    1, 7, 63, 511, 4096,
    5000, 6000, 32768, 65536, 131072,
    1048576, 4194304, 4194305, 8388608
};

static const size_t k_size_count = sizeof(k_sizes) / sizeof(k_sizes[0]);
static const char *k_case_dir_name = "interceptor_io_cases";
static const char *k_file_prefix = "case_";

static uint32_t mix32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static uint8_t pattern_byte(int api_id, size_t total_size, size_t offset)
{
    uint32_t x = 2166136261u;
    x ^= (uint32_t)api_id;
    x = mix32(x);
    x ^= (uint32_t)total_size;
    x = mix32(x);
    x ^= (uint32_t)(total_size >> 32);
    x = mix32(x);
    x ^= (uint32_t)offset;
    x = mix32(x);
    x ^= (uint32_t)(offset >> 32);
    x = mix32(x);
    return (uint8_t)(x & 0xff);
}

static void fill_pattern(uint8_t *buf, size_t len, int api_id, size_t total_size, size_t base_off)
{
    for (size_t i = 0; i < len; ++i) {
        buf[i] = pattern_byte(api_id, total_size, base_off + i);
    }
}

static void build_expected(uint8_t *buf, size_t size, int api_id)
{
    fill_pattern(buf, size, api_id, size, 0);
}

static int ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;
        }
        fprintf(stderr, "[FAIL] %s exists but is not a directory\n", path);
        return -1;
    }
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        perror("mkdir");
        return -1;
    }
    return 0;
}

static int join_path(char *buf, size_t cap, const char *base, const char *name)
{
    int n = snprintf(buf, cap, "%s/%s", base, name);
    if (n < 0 || (size_t)n >= cap) {
        fprintf(stderr, "[FAIL] path too long: %s/%s\n", base, name);
        return -1;
    }
    return 0;
}

static int make_case_dir(char *buf, size_t cap, const char *root)
{
    if (join_path(buf, cap, root, k_case_dir_name) != 0) {
        return -1;
    }
    return ensure_dir(buf);
}

static int write_full(int fd, const uint8_t *buf, size_t len)
{
    size_t done = 0;
    while (done < len) {
        ssize_t ret = write(fd, buf + done, len - done);
        if (ret < 0) {
            return -1;
        }
        done += (size_t)ret;
    }
    return 0;
}

static int pwrite_full(int fd, const uint8_t *buf, size_t len, off_t off)
{
    size_t done = 0;
    while (done < len) {
        ssize_t ret = pwrite(fd, buf + done, len - done, off + (off_t)done);
        if (ret < 0) {
            return -1;
        }
        done += (size_t)ret;
    }
    return 0;
}

static int pwrite64_full(int fd, const uint8_t *buf, size_t len, off64_t off)
{
    size_t done = 0;
    while (done < len) {
        ssize_t ret = pwrite64(fd, buf + done, len - done, off + (off64_t)done);
        if (ret < 0) {
            return -1;
        }
        done += (size_t)ret;
    }
    return 0;
}

static int read_full(int fd, uint8_t *buf, size_t len)
{
    size_t done = 0;
    while (done < len) {
        ssize_t ret = read(fd, buf + done, len - done);
        if (ret < 0) {
            return -1;
        }
        if (ret == 0) {
            break;
        }
        done += (size_t)ret;
    }
    return done == len ? 0 : -1;
}

static int pread_full(int fd, uint8_t *buf, size_t len, off_t off)
{
    size_t done = 0;
    while (done < len) {
        ssize_t ret = pread(fd, buf + done, len - done, off + (off_t)done);
        if (ret < 0) {
            return -1;
        }
        if (ret == 0) {
            break;
        }
        done += (size_t)ret;
    }
    return done == len ? 0 : -1;
}

static int pread64_full(int fd, uint8_t *buf, size_t len, off64_t off)
{
    size_t done = 0;
    while (done < len) {
        ssize_t ret = pread64(fd, buf + done, len - done, off + (off64_t)done);
        if (ret < 0) {
            return -1;
        }
        if (ret == 0) {
            break;
        }
        done += (size_t)ret;
    }
    return done == len ? 0 : -1;
}

static void split_three(size_t total, size_t parts[3])
{
    parts[0] = total / 3;
    parts[1] = total / 3;
    parts[2] = total - parts[0] - parts[1];
    if (parts[0] == 0 && total > 0) {
        parts[0] = 1;
        if (parts[1] > 0) {
            parts[1]--;
        } else if (parts[2] > 0) {
            parts[2]--;
        }
    }
    if (parts[1] == 0 && total > 1) {
        parts[1] = 1;
        if (parts[2] > 0) {
            parts[2]--;
        } else if (parts[0] > 1) {
            parts[0]--;
        }
    }
}

static int write_case_write(int fd, const uint8_t *data, size_t size)
{
    size_t parts[3];
    split_three(size, parts);
    size_t off = 0;
    for (int i = 0; i < 3; ++i) {
        if (parts[i] == 0) {
            continue;
        }
        if (write_full(fd, data + off, parts[i]) != 0) {
            return -1;
        }
        off += parts[i];
    }
    return 0;
}

static int write_case_pwrite_common(int fd, const uint8_t *data, size_t size, int use64)
{
    size_t parts[3];
    split_three(size, parts);
    off_t offsets[3];
    offsets[0] = (off_t)parts[1];
    offsets[1] = 0;
    offsets[2] = (off_t)(parts[0] + parts[1]);
    const uint8_t *ptrs[3];
    ptrs[0] = data + parts[0];
    ptrs[1] = data;
    ptrs[2] = data + parts[0] + parts[1];
    for (int i = 0; i < 3; ++i) {
        if (parts[i] == 0) {
            continue;
        }
        int rc = use64
            ? pwrite64_full(fd, ptrs[i], parts[i], (off64_t)offsets[i])
            : pwrite_full(fd, ptrs[i], parts[i], offsets[i]);
        if (rc != 0) {
            return -1;
        }
    }
    return 0;
}

static int write_case_writev(int fd, const uint8_t *data, size_t size)
{
    size_t parts[3];
    split_three(size, parts);
    struct iovec iov[3];
    size_t off = 0;
    int cnt = 0;
    for (int i = 0; i < 3; ++i) {
        if (parts[i] == 0) {
            continue;
        }
        iov[cnt].iov_base = (void *)(data + off);
        iov[cnt].iov_len = parts[i];
        off += parts[i];
        cnt++;
    }
    ssize_t ret = writev(fd, iov, cnt);
    return ret == (ssize_t)size ? 0 : -1;
}

static int write_case_pwritev_common(int fd, const uint8_t *data, size_t size, int use64)
{
    size_t half = size / 2;
    size_t tail = size - half;
    size_t first_parts[3];
    size_t second_parts[3];
    split_three(half, first_parts);
    split_three(tail, second_parts);
    struct iovec iov1[3];
    struct iovec iov2[3];
    int c1 = 0;
    int c2 = 0;
    size_t off = 0;
    for (int i = 0; i < 3; ++i) {
        if (first_parts[i] == 0) {
            continue;
        }
        iov1[c1].iov_base = (void *)(data + off);
        iov1[c1].iov_len = first_parts[i];
        off += first_parts[i];
        c1++;
    }
    for (int i = 0; i < 3; ++i) {
        if (second_parts[i] == 0) {
            continue;
        }
        iov2[c2].iov_base = (void *)(data + off);
        iov2[c2].iov_len = second_parts[i];
        off += second_parts[i];
        c2++;
    }
    ssize_t ret1 = use64
        ? pwritev64(fd, iov1, c1, 0)
        : pwritev(fd, iov1, c1, 0);
    if (ret1 != (ssize_t)half) {
        return -1;
    }
    ssize_t ret2 = use64
        ? pwritev64(fd, iov2, c2, (off64_t)half)
        : pwritev(fd, iov2, c2, (off_t)half);
    return ret2 == (ssize_t)tail ? 0 : -1;
}

static int write_one_file(const char *path, const char *api_name, int api_id, size_t size)
{
    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd < 0) {
        perror("open");
        return -1;
    }
    uint8_t *data = (uint8_t *)malloc(size ? size : 1);
    uint8_t *verify = (uint8_t *)malloc(size ? size : 1);
    if (data == NULL || verify == NULL) {
        fprintf(stderr, "[FAIL] malloc failed for %s size=%zu\n", api_name, size);
        close(fd);
        free(data);
        free(verify);
        return -1;
    }
    build_expected(data, size, api_id);

    int rc = 0;
    if (strcmp(api_name, "write") == 0) {
        rc = write_case_write(fd, data, size);
    } else if (strcmp(api_name, "pwrite") == 0) {
        rc = write_case_pwrite_common(fd, data, size, 0);
    } else if (strcmp(api_name, "pwrite64") == 0) {
        rc = write_case_pwrite_common(fd, data, size, 1);
    } else if (strcmp(api_name, "writev") == 0) {
        rc = write_case_writev(fd, data, size);
    } else if (strcmp(api_name, "pwritev") == 0) {
        rc = write_case_pwritev_common(fd, data, size, 0);
    } else if (strcmp(api_name, "pwritev64") == 0) {
        rc = write_case_pwritev_common(fd, data, size, 1);
    } else {
        fprintf(stderr, "[FAIL] unknown write api %s\n", api_name);
        rc = -1;
    }
    if (rc != 0) {
        fprintf(stderr, "[FAIL] %s write failed size=%zu errno=%d\n", api_name, size, errno);
        close(fd);
        free(data);
        free(verify);
        return -1;
    }
    if (fsync(fd) != 0) {
        perror("fsync");
        close(fd);
        free(data);
        free(verify);
        return -1;
    }
    if (pread_full(fd, verify, size, 0) != 0) {
        fprintf(stderr, "[FAIL] local verify read failed %s size=%zu errno=%d\n", api_name, size, errno);
        close(fd);
        free(data);
        free(verify);
        return -1;
    }
    if (memcmp(data, verify, size) != 0) {
        fprintf(stderr, "[FAIL] local verify mismatch %s size=%zu\n", api_name, size);
        close(fd);
        free(data);
        free(verify);
        return -1;
    }
    close(fd);
    free(data);
    free(verify);
    printf("[PASS] write api=%s size=%zu\n", api_name, size);
    return 0;
}

static int read_case_read(int fd, uint8_t *buf, size_t size)
{
    if (lseek(fd, 0, SEEK_SET) < 0) {
        return -1;
    }
    return read_full(fd, buf, size);
}

static int read_case_pread_common(int fd, uint8_t *buf, size_t size, int use64)
{
    size_t chunk = 65536;
    size_t done = 0;
    while (done < size) {
        size_t left = size - done;
        size_t cur = left < chunk ? left : chunk;
        int rc = use64
            ? pread64_full(fd, buf + done, cur, (off64_t)done)
            : pread_full(fd, buf + done, cur, (off_t)done);
        if (rc != 0) {
            return -1;
        }
        done += cur;
    }
    return 0;
}

static int read_case_readv(int fd, uint8_t *buf, size_t size)
{
    if (lseek(fd, 0, SEEK_SET) < 0) {
        return -1;
    }
    size_t done = 0;
    while (done < size) {
        size_t left = size - done;
        size_t p1 = left > 4096 ? 4096 : left;
        size_t p2 = left > p1 ? (left - p1 > 6000 ? 6000 : left - p1) : 0;
        size_t p3 = left - p1 - p2;
        struct iovec iov[3];
        int cnt = 0;
        iov[cnt].iov_base = buf + done;
        iov[cnt].iov_len = p1;
        cnt++;
        if (p2 > 0) {
            iov[cnt].iov_base = buf + done + p1;
            iov[cnt].iov_len = p2;
            cnt++;
        }
        if (p3 > 0) {
            iov[cnt].iov_base = buf + done + p1 + p2;
            iov[cnt].iov_len = p3;
            cnt++;
        }
        ssize_t ret = readv(fd, iov, cnt);
        if (ret < 0) {
            return -1;
        }
        if (ret == 0) {
            break;
        }
        done += (size_t)ret;
    }
    return done == size ? 0 : -1;
}

static int read_case_preadv64(int fd, uint8_t *buf, size_t size)
{
    size_t done = 0;
    while (done < size) {
        size_t left = size - done;
        size_t p1 = left > 4096 ? 4096 : left;
        size_t p2 = left > p1 ? (left - p1 > 6000 ? 6000 : left - p1) : 0;
        size_t p3 = left - p1 - p2;
        struct iovec iov[3];
        int cnt = 0;
        iov[cnt].iov_base = buf + done;
        iov[cnt].iov_len = p1;
        cnt++;
        if (p2 > 0) {
            iov[cnt].iov_base = buf + done + p1;
            iov[cnt].iov_len = p2;
            cnt++;
        }
        if (p3 > 0) {
            iov[cnt].iov_base = buf + done + p1 + p2;
            iov[cnt].iov_len = p3;
            cnt++;
        }
        ssize_t ret = preadv64(fd, iov, cnt, (off64_t)done);
        if (ret < 0) {
            return -1;
        }
        if (ret == 0) {
            break;
        }
        done += (size_t)ret;
    }
    return done == size ? 0 : -1;
}

static int read_and_check(const char *path, const char *write_api_name, int write_api_id, size_t size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return -1;
    }
    uint8_t *expected = (uint8_t *)malloc(size ? size : 1);
    uint8_t *actual = (uint8_t *)malloc(size ? size : 1);
    if (expected == NULL || actual == NULL) {
        fprintf(stderr, "[FAIL] malloc failed in read verify path=%s size=%zu\n", path, size);
        close(fd);
        free(expected);
        free(actual);
        return -1;
    }
    build_expected(expected, size, write_api_id);
    for (size_t i = 0; i < sizeof(k_read_cases) / sizeof(k_read_cases[0]); ++i) {
        memset(actual, 0, size);
        int rc = 0;
        if (strcmp(k_read_cases[i].name, "read") == 0) {
            rc = read_case_read(fd, actual, size);
        } else if (strcmp(k_read_cases[i].name, "pread") == 0) {
            rc = read_case_pread_common(fd, actual, size, 0);
        } else if (strcmp(k_read_cases[i].name, "pread64") == 0) {
            rc = read_case_pread_common(fd, actual, size, 1);
        } else if (strcmp(k_read_cases[i].name, "readv") == 0) {
            rc = read_case_readv(fd, actual, size);
        } else if (strcmp(k_read_cases[i].name, "preadv64") == 0) {
            rc = read_case_preadv64(fd, actual, size);
        }
        if (rc != 0) {
            fprintf(stderr, "[FAIL] read api=%s path=%s size=%zu errno=%d\n",
                k_read_cases[i].name, path, size, errno);
            close(fd);
            free(expected);
            free(actual);
            return -1;
        }
        if (memcmp(expected, actual, size) != 0) {
            fprintf(stderr, "[FAIL] data mismatch read_api=%s write_api=%s size=%zu path=%s\n",
                k_read_cases[i].name, write_api_name, size, path);
            close(fd);
            free(expected);
            free(actual);
            return -1;
        }
        printf("[PASS] read api=%s write_api=%s size=%zu\n",
            k_read_cases[i].name, write_api_name, size);
    }
    close(fd);
    free(expected);
    free(actual);
    return 0;
}

static int run_write_mode(const char *root)
{
    char case_dir[4096];
    if (make_case_dir(case_dir, sizeof(case_dir), root) != 0) {
        return 1;
    }
    for (size_t i = 0; i < sizeof(k_write_cases) / sizeof(k_write_cases[0]); ++i) {
        for (size_t j = 0; j < k_size_count; ++j) {
            char filename[256];
            char path[4096];
            snprintf(filename, sizeof(filename), "%s%s_%zu.bin", k_file_prefix, k_write_cases[i].name, k_sizes[j]);
            if (join_path(path, sizeof(path), case_dir, filename) != 0) {
                return 1;
            }
            if (write_one_file(path, k_write_cases[i].name, k_write_cases[i].id, k_sizes[j]) != 0) {
                return 1;
            }
        }
    }
    printf("[PASS] all write cases completed under %s\n", case_dir);
    return 0;
}

static int parse_case_file(const char *name, char *api_name, size_t api_cap, size_t *size_out)
{
    if (strncmp(name, k_file_prefix, strlen(k_file_prefix)) != 0) {
        return -1;
    }
    const char *p = name + strlen(k_file_prefix);
    const char *us = strrchr(p, '_');
    const char *dot = strrchr(p, '.');
    if (us == NULL || dot == NULL || us >= dot) {
        return -1;
    }
    size_t api_len = (size_t)(us - p);
    if (api_len == 0 || api_len + 1 > api_cap) {
        return -1;
    }
    memcpy(api_name, p, api_len);
    api_name[api_len] = '\0';
    errno = 0;
    unsigned long long val = strtoull(us + 1, NULL, 10);
    if (errno != 0) {
        return -1;
    }
    *size_out = (size_t)val;
    return 0;
}

static int write_api_id_by_name(const char *name)
{
    for (size_t i = 0; i < sizeof(k_write_cases) / sizeof(k_write_cases[0]); ++i) {
        if (strcmp(k_write_cases[i].name, name) == 0) {
            return k_write_cases[i].id;
        }
    }
    return -1;
}

static int run_read_mode(const char *root)
{
    char case_dir[4096];
    if (make_case_dir(case_dir, sizeof(case_dir), root) != 0) {
        return 1;
    }
    DIR *dir = opendir(case_dir);
    if (dir == NULL) {
        perror("opendir");
        return 1;
    }
    struct dirent *ent;
    int file_count = 0;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        char api_name[64];
        size_t size = 0;
        if (parse_case_file(ent->d_name, api_name, sizeof(api_name), &size) != 0) {
            continue;
        }
        int api_id = write_api_id_by_name(api_name);
        if (api_id < 0) {
            fprintf(stderr, "[FAIL] unknown write api in file name: %s\n", ent->d_name);
            closedir(dir);
            return 1;
        }
        char path[4096];
        if (join_path(path, sizeof(path), case_dir, ent->d_name) != 0) {
            closedir(dir);
            return 1;
        }
        if (read_and_check(path, api_name, api_id, size) != 0) {
            closedir(dir);
            return 1;
        }
        file_count++;
    }
    closedir(dir);
    if (file_count == 0) {
        fprintf(stderr, "[FAIL] no case files found under %s\n", case_dir);
        return 1;
    }
    printf("[PASS] all read cases completed under %s, files=%d\n", case_dir, file_count);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <write|read> <mount_dir>\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "write") == 0) {
        return run_write_mode(argv[2]);
    }
    if (strcmp(argv[1], "read") == 0) {
        return run_read_mode(argv[2]);
    }
    fprintf(stderr, "unknown mode: %s\n", argv[1]);
    return 2;
}
