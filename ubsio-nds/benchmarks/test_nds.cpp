/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

/*
 * Benchmark for libascend_nds.so (GDS-shape NDS facade) public API.
 *
 * Measures multi-threaded throughput of four SSD->HBM/DRAM paths:
 *   1) ndsFileBatchIOSubmit + ndsFileBatchIOGetStatus  (async batch, SSD->HBM)
 *   2) ndsFileRead                                       (sync,      SSD->HBM)
 *   3) pread                                             (baseline,  SSD->DRAM)
 *   4) pread + aclrtMemcpy                              (staged,    SSD->DRAM->HBM)
 *
 * Build (on an Ascend box with the ACL toolkit + libascend_nds.so installed):
 *   g++ -O2 -std=c++17 -Wall -I$UBSIO_NDS/dist/include \
 *       test_nds.cpp -o test_nds \
 *       -L$UBSIO_NDS/dist/lib -lascend_nds \
 *       -L$ASCEND_TOOLKIT_HOME/lib64 -lascendcl -lpthread
 *
 * Run (requires /dev/p2p_device and a topology-capable block stack):
 *   ./test_nds /path/to/nds-supported-test-file 1024 64 1 0 1
 *   args: <file> <length_kb> <gran_kb> <threads> <device_id> <repeat_num>
 */

#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cstdint>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <algorithm>
#include <vector>
#include <string>
#include <iostream>
#include <acl/acl.h>
#include "ascend_nds.h"

constexpr int DEFAULT_REPEAT = 10;
constexpr int NDS_GETEV_TIMEOUT_SEC = 30;
constexpr unsigned BATCH_MAX_DEPTH = 1024u;
constexpr unsigned ASYNC_BATCH_LANES = 4u;

struct ThreadData {
    void *hbm_seg;
    void *dram_seg;
    int fd;
    NDSfileHandle_t fh;
    off_t file_off0;
    size_t granularity;
    uint32_t blocks;
    int repeat;
    int deviceId;
    int result{0};
};

static bool IsPowerOfTwo(unsigned int n)
{
    return n > 0 && (n & (n - 1)) == 0;
}

static void ReportPath(const char *method, unsigned gran_kb, unsigned long len_kb,
                       double time_s, double mb, FILE *csv)
{
    double bw = (time_s > 0.0) ? mb / time_s : 0.0;
    printf("%-22s time:%.4lfs  read %lu MB  throughput:%.2lf MB/s\n",
           method, time_s, static_cast<unsigned long>(mb), bw);
    if (csv != nullptr) {
        fprintf(csv, "%s, %u, %lu, %.4lf, %.2lf\n",
                method, gran_kb, len_kb / 1024, time_s, bw);
    }
}

static bool RunPath(const char *method, ThreadData *td, unsigned num_threads,
                    void *(*fn)(void *), unsigned gran_kb, unsigned long len_kb,
                    double mb, FILE *csv)
{
    std::vector<pthread_t> threads(num_threads);
    struct timeval t0, t1;
    gettimeofday(&t0, nullptr);
    unsigned started = 0;
    for (; started < num_threads; ++started) {
        td[started].result = 0;
        int ret = pthread_create(&threads[started], nullptr, fn, &td[started]);
        if (ret != 0) {
            fprintf(stderr, "%s: pthread_create[%u] failed ret=%d\n", method, started, ret);
            break;
        }
    }
    bool success = started == num_threads;
    for (unsigned i = 0; i < started; ++i) {
        int ret = pthread_join(threads[i], nullptr);
        if (ret != 0) {
            fprintf(stderr, "%s: pthread_join[%u] failed ret=%d\n", method, i, ret);
            success = false;
        } else if (td[i].result != 0) {
            success = false;
        }
    }
    if (!success) {
        return false;
    }
    gettimeofday(&t1, nullptr);
    double us = static_cast<double>(t1.tv_sec - t0.tv_sec) * 1e6 +
                static_cast<double>(t1.tv_usec - t0.tv_usec);
    us /= static_cast<double>(td[0].repeat);
    ReportPath(method, gran_kb, len_kb, us / 1e6, mb, csv);
    return true;
}

static bool RunSingle(const char *method, int (*fn)(void *), void *ctx, int repeat,
                      unsigned gran_kb, unsigned long len_kb, double mb, FILE *csv)
{
    struct timeval t0, t1;
    gettimeofday(&t0, nullptr);
    int r = fn(ctx);
    gettimeofday(&t1, nullptr);
    if (r != 0) {
        fprintf(stderr, "%s failed ret=%d\n", method, r);
        return false;
    }
    double us = static_cast<double>(t1.tv_sec - t0.tv_sec) * 1e6 +
                static_cast<double>(t1.tv_usec - t0.tv_usec);
    us /= static_cast<double>(repeat);
    ReportPath(method, gran_kb, len_kb, us / 1e6, mb, csv);
    return true;
}

struct BatchCtx {
    NDSfileHandle_t fh;
    void *hbm;
    size_t length;
    size_t gran;
    int repeat;
    unsigned flags;
};

static int RunNdsBatchAsync(void *arg)
{
    BatchCtx *c = static_cast<BatchCtx *>(arg);
    uint32_t total = static_cast<uint32_t>(c->length / c->gran);
    uint32_t depth = std::min(total, static_cast<uint32_t>(BATCH_MAX_DEPTH));
    uint32_t num_chunks = (total + depth - 1) / depth;
    uint32_t lanes = std::min(static_cast<uint32_t>(ASYNC_BATCH_LANES), num_chunks);
    if (lanes == 0) {
        lanes = 1;
    }
    printf("[nds_batch_async] total_blocks=%u batch_depth=%u lanes=%u (single-threaded, ring-pipelined)\n",
           total, depth, lanes);

    std::vector<NDSfileBatchHandle_t> handles(lanes, nullptr);
    int r = 0;
    for (uint32_t i = 0; i < lanes; ++i) {
        r = ndsFileBatchIOSetUp(&handles[i], depth);
        if (r != 0 || handles[i] == nullptr) {
            fprintf(stderr, "[nds_batch_async] BatchIOSetUp[%u] failed ret=%d (depth=%u)\n", i, r, depth);
            if (r == 0) {
                r = -EIO;
            }
            for (uint32_t j = 0; j < i; ++j) {
                ndsFileBatchIODestroy(handles[j]);
            }
            return r;
        }
    }

    std::vector<NDSfileIOParams_t> params(depth);
    NDSfileIOVec_t iov{c->hbm, c->gran};
    std::vector<NDSfileIOEvents_t> evs(depth);
    std::vector<uint32_t> outstanding(lanes, 0);
    struct timespec ts { NDS_GETEV_TIMEOUT_SEC, 0 };

    auto submit = [&](uint32_t lane, uint32_t &nextBlock) {
        const uint32_t count = std::min(depth, total - nextBlock);
        for (uint32_t j = 0; j < count; ++j) {
            const uint32_t block = nextBlock + j;
            const auto offset = static_cast<off_t>(block * c->gran);
            params[j] = {&iov, offset, offset, 1, c->fh, NDS_FILE_READ,
                         reinterpret_cast<void *>(static_cast<uintptr_t>(block))};
        }
        const int accepted = ndsFileBatchIOSubmit(handles[lane], count, params.data(), c->flags);
        if (accepted <= 0 || static_cast<unsigned>(accepted) > count) {
            fprintf(stderr, "[nds_batch_async] Submit ret=%d (lane=%u count=%u)\n", accepted, lane, count);
            return accepted < 0 ? accepted : -EIO;
        }
        outstanding[lane] = static_cast<unsigned>(accepted);
        nextBlock += outstanding[lane];
        return 0;
    };

    for (int rep = 0; rep < c->repeat; ++rep) {
        uint32_t nextBlock = 0;
        uint32_t done = 0;
        for (uint32_t i = 0; i < lanes && nextBlock < total; ++i) {
            r = submit(i, nextBlock);
            if (r != 0) {
                goto out;
            }
        }
        while (done < total) {
            for (uint32_t i = 0; i < lanes; ++i) {
                if (outstanding[i] == 0) {
                    continue;
                }
                uint32_t k = outstanding[i];
                unsigned got = k;
                r = ndsFileBatchIOGetStatus(handles[i], k, &got, evs.data(), &ts);
                if (r != 0 || got != k) {
                    fprintf(stderr, "[nds_batch_async] GetStatus ret=%d got=%u (lane=%u k=%u)\n",
                            r, got, i, k);
                    r = r != 0 ? r : -ETIMEDOUT;
                    goto out;
                }
                for (unsigned m = 0; m < got; ++m) {
                    if (evs[m].status != NDS_FILE_COMPLETE) {
                        fprintf(stderr, "[nds_batch_async] event status=%d ret=%d (lane=%u)\n",
                                evs[m].status, evs[m].ret, i);
                        r = -EIO;
                        goto out;
                    }
                }
                done += k;
                outstanding[i] = 0;
                if (nextBlock < total) {
                    r = submit(i, nextBlock);
                    if (r != 0) {
                        goto out;
                    }
                }
            }
        }
    }
out:
    for (uint32_t i = 0; i < lanes; ++i) {
        if (handles[i] != nullptr) {
            ndsFileBatchIODestroy(handles[i]);
        }
    }
    return r;
}

static void *ThreadNdsReadSync(void *arg)
{
    ThreadData *d = static_cast<ThreadData *>(arg);
    for (int rep = 0; rep < d->repeat; ++rep) {
        for (uint32_t j = 0; j < d->blocks; ++j) {
            int r = ndsFileRead(d->fh, d->hbm_seg, d->granularity,
                                d->file_off0 + static_cast<off_t>(j * d->granularity),
                                static_cast<off_t>(j * d->granularity));
            if (r != 0) {
                fprintf(stderr, "[nds_read_sync] ndsFileRead[%u] ret=%d\n", j, r);
                d->result = r;
                return nullptr;
            }
        }
    }
    return nullptr;
}

static void *ThreadPreadCpu(void *arg)
{
    ThreadData *d = static_cast<ThreadData *>(arg);
    for (int rep = 0; rep < d->repeat; ++rep) {
        for (uint32_t j = 0; j < d->blocks; ++j) {
            ssize_t n = pread(d->fd, static_cast<char *>(d->dram_seg) + j * d->granularity,
                              d->granularity, d->file_off0 + static_cast<off_t>(j * d->granularity));
            if (n != static_cast<ssize_t>(d->granularity)) {
                fprintf(stderr, "[pread_ssd_dram] pread[%u] ret=%zd\n", j, n);
                d->result = -EIO;
                return nullptr;
            }
        }
    }
    return nullptr;
}

static void *ThreadPreadNpu(void *arg)
{
    ThreadData *d = static_cast<ThreadData *>(arg);
    aclrtContext ctx;
    aclError e = aclrtSetDevice(d->deviceId);
    if (e != ACL_SUCCESS) {
        fprintf(stderr, "[pread+memcpy_npu] aclrtSetDevice ret=%d\n", static_cast<int>(e));
        d->result = e;
        return nullptr;
    }
    e = aclrtCreateContext(&ctx, d->deviceId);
    if (e != ACL_SUCCESS) {
        fprintf(stderr, "[pread+memcpy_npu] aclrtCreateContext ret=%d\n", static_cast<int>(e));
        d->result = e;
        return nullptr;
    }
    for (int rep = 0; rep < d->repeat && d->result == 0; ++rep) {
        for (uint32_t j = 0; j < d->blocks; ++j) {
            ssize_t n = pread(d->fd, static_cast<char *>(d->dram_seg) + j * d->granularity,
                              d->granularity, d->file_off0 + static_cast<off_t>(j * d->granularity));
            if (n != static_cast<ssize_t>(d->granularity)) {
                fprintf(stderr, "[pread+memcpy_npu] pread[%u] ret=%zd\n", j, n);
                d->result = -EIO;
                break;
            }
            e = aclrtMemcpy(static_cast<char *>(d->hbm_seg) + j * d->granularity, d->granularity,
                            static_cast<char *>(d->dram_seg) + j * d->granularity, d->granularity,
                            ACL_MEMCPY_HOST_TO_DEVICE);
            if (e != ACL_SUCCESS) {
                fprintf(stderr, "[pread+memcpy_npu] aclrtMemcpy[%u] ret=%d\n", j, static_cast<int>(e));
                d->result = e;
                break;
            }
        }
    }
    aclrtDestroyContext(ctx);
    return nullptr;
}

int main(int argc, const char *argv[])
{
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <file> <length_kb> <gran_kb> <threads> <device_id> <repeat_num>\n",
                argv[0]);
        return 1;
    }
    const char *file_name = argv[1];
    unsigned long len_kb = strtoul(argv[2], nullptr, 10);
    unsigned gran_kb = static_cast<unsigned>(strtoul(argv[3], nullptr, 10));
    unsigned num_threads = static_cast<unsigned>(strtoul(argv[4], nullptr, 10));
    int deviceId = (argc >= 6) ? atoi(argv[5]) : 0;
    int repeat = (argc >= 7) ? atoi(argv[6]) : DEFAULT_REPEAT;
    if (repeat <= 0) {
        repeat = DEFAULT_REPEAT;
    }
    if (len_kb == 0 || gran_kb == 0 || !IsPowerOfTwo(num_threads)) {
        fprintf(stderr, "invalid args: len_kb=%lu gran_kb=%u threads=%u (threads must be power of 2)\n",
                len_kb, gran_kb, num_threads);
        return 1;
    }
    size_t length = len_kb * 1024;
    size_t gran = static_cast<size_t>(gran_kb) * 1024;
    if (length % gran != 0) {
        fprintf(stderr, "length must be divisible by gran: length=%zu gran=%zu\n", length, gran);
        return 1;
    }
    size_t segment = length / num_threads;
    if (segment == 0 || segment % gran != 0) {
        fprintf(stderr, "length/threads must be >= gran and divisible: length=%zu seg=%zu gran=%zu\n",
                length, segment, gran);
        return 1;
    }
    uint32_t blocks = static_cast<uint32_t>(segment / gran);
    double mb = static_cast<double>(length) / (1024.0 * 1024.0);

    FILE *csv = fopen("statistic_out.csv", "a");
    if (csv == nullptr) {
        fprintf(stderr, "cannot open statistic_out.csv\n");
        return 1;
    }
    fprintf(csv, "Method, Granularity_KB, ReadSize_MB, Time_s, Throughput_MB_S\n");

    printf("file=%s length=%zuKB gran=%zuKB threads=%u deviceId=%d repeat=%d blocks/thread=%u\n",
           file_name, length / 1024, gran / 1024, num_threads, deviceId, repeat, blocks);

    aclrtContext context = nullptr;
    aclrtStream stream = nullptr;
    void *hbm = nullptr;
    void *dram = nullptr;
    int fd = -1;
    NDSfileHandle_t fh = nullptr;
    bool acl_ok = false;
    bool dev_ok = false;
    bool ctx_ok = false;
    bool drv_ok = false;
    bool fs_ok = false;
    bool handle_ok = false;
    bool buf_ok = false;
    int ver = 0;
    int exit_code = EXIT_FAILURE;

    aclError e = aclInit(nullptr);
    if (e != ACL_SUCCESS) {
        fprintf(stderr, "aclInit failed ret=%d\n", static_cast<int>(e));
        goto teardown;
    }
    acl_ok = true;
    e = aclrtSetDevice(deviceId);
    if (e != ACL_SUCCESS) {
        fprintf(stderr, "aclrtSetDevice failed ret=%d\n", static_cast<int>(e));
        goto teardown;
    }
    dev_ok = true;
    e = aclrtCreateContext(&context, deviceId);
    if (e != ACL_SUCCESS) {
        fprintf(stderr, "aclrtCreateContext failed ret=%d\n", static_cast<int>(e));
        goto teardown;
    }
    ctx_ok = true;
    aclrtCreateStream(&stream);

    e = aclrtMalloc(&hbm, length, ACL_MEM_MALLOC_HUGE_ONLY);
    if (e != ACL_SUCCESS || hbm == nullptr) {
        fprintf(stderr, "aclrtMalloc failed ret=%d\n", static_cast<int>(e));
        goto teardown;
    }
    aclrtMemset(hbm, length, 0xFF, length);

    if (posix_memalign(&dram, 2 * 1024 * 1024, length) != 0) {
        fprintf(stderr, "posix_memalign failed\n");
        goto teardown;
    }
    memset(dram, 0, length);

    fd = open(file_name, O_RDONLY | O_DIRECT);
    if (fd < 0) {
        fprintf(stderr, "open %s failed errno=%d\n", file_name, errno);
        goto teardown;
    }

    {
        int r = ndsFileDriverOpen();
        if (r != 0) {
            fprintf(stderr, "ndsFileDriverOpen failed ret=%d\n", r);
            goto teardown;
        }
        drv_ok = true;
        NDSfileFsDesc_t fs { &fd, 1u, 0u };
        r = ndsFileRegisterFs(&fs);
        if (r != 0) {
            fprintf(stderr, "ndsFileRegisterFs failed ret=%d\n", r);
            goto teardown;
        }
        fs_ok = true;
        NDSfileDescr_t descr { NDS_FILE_HANDLE_TYPE_OPAQUE_FD, { fd } };
        r = ndsFileHandleRegister(&fh, &descr);
        if (r != 0) {
            fprintf(stderr, "ndsFileHandleRegister failed ret=%d\n", r);
            goto teardown;
        }
        handle_ok = true;
        r = ndsFileBufRegister(hbm, length, 0);
        if (r != 0) {
            fprintf(stderr, "warn: ndsFileBufRegister ret=%d (continuing)\n", r);
        } else {
            buf_ok = true;
        }
        ndsFileGetVersion(&ver);
        printf("nds API version=%d\n", ver);
    }

    {
        std::vector<ThreadData> td(num_threads);
        for (unsigned i = 0; i < num_threads; ++i) {
            td[i].hbm_seg = static_cast<char *>(hbm) + i * segment;
            td[i].dram_seg = static_cast<char *>(dram) + i * segment;
            td[i].fd = fd;
            td[i].fh = fh;
            td[i].file_off0 = static_cast<off_t>(i * segment);
            td[i].granularity = gran;
            td[i].blocks = blocks;
            td[i].repeat = repeat;
            td[i].deviceId = deviceId;
        }
        BatchCtx bc { fh, hbm, length, gran, repeat, buf_ok ? NDS_FILE_IO_REGISTERED_MEM : 0u };
        const bool batch_ok = RunSingle("nds_batch_async", RunNdsBatchAsync, &bc, repeat,
                                       gran_kb, len_kb, mb, csv);
        const bool nds_sync_ok = RunPath("nds_read_sync", td.data(), num_threads, ThreadNdsReadSync,
                                        gran_kb, len_kb, mb, csv);
        const bool pread_ok = RunPath("pread_ssd_dram", td.data(), num_threads, ThreadPreadCpu,
                                     gran_kb, len_kb, mb, csv);
        const bool memcpy_ok = RunPath("pread+memcpy_npu", td.data(), num_threads, ThreadPreadNpu,
                                      gran_kb, len_kb, mb, csv);
        if (batch_ok && nds_sync_ok && pread_ok && memcpy_ok) {
            exit_code = EXIT_SUCCESS;
        }
    }

teardown:
    if (drv_ok && buf_ok) {
        ndsFileBufDeregister(hbm);
    }
    if (drv_ok && handle_ok) {
        ndsFileHandleDeregister(fh);
    }
    if (drv_ok && fs_ok) {
        NDSfileFsDesc_t fs { &fd, 1u, 0u };
        ndsFileDeregisterFs(&fs);
    }
    if (drv_ok) {
        ndsFileDriverClose();
    }
    if (fd >= 0) {
        close(fd);
    }
    free(dram);
    if (hbm != nullptr) {
        aclrtFree(hbm);
    }
    if (stream != nullptr) {
        aclrtSynchronizeStream(stream);
        aclrtDestroyStream(stream);
    }
    if (ctx_ok) {
        aclrtDestroyContext(context);
    }
    if (dev_ok) {
        aclrtResetDevice(deviceId);
    }
    if (acl_ok) {
        aclFinalize();
    }
    fclose(csv);
    return exit_code;
}
