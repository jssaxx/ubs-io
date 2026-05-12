/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <functional>
#include <sys/sysinfo.h>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cerrno>
#include <sys/syscall.h>
#include <linux/version.h>
#include "mms_comm.h"
#include "mms_log.h"
#include "mms_mem_mgr.h"

namespace ock {
namespace mms {

void ValueIndexMemCfg::Calculate()
{
    uint64_t pageSize = GetDevicePageSize();
    uint64_t usableTotalMem = totalMemSize - (totalMemSize % pageSize);

    double density =
        (minBlockMemRatio / static_cast<double>(minBlockSize)) + (maxBlockMemRatio / static_cast<double>(maxBlockSize));

    double indexToValueRatio = density * static_cast<double>(indexNodeSize);
    constexpr double factor = 1.0;
    double totalValueMemIdeal = static_cast<double>(usableTotalMem) / (factor + indexToValueRatio);

    uint64_t alignedValueMem = static_cast<uint64_t>(totalValueMemIdeal);
    alignedValueMem = alignedValueMem - (alignedValueMem % pageSize);  // 向下对齐
    uint64_t alignedIndexMem = usableTotalMem - alignedValueMem;

    valueMemSize = alignedValueMem;
    indexMemSize = alignedIndexMem;
}

static bool CheckMemEnough(MemMgrOptions &options)
{
    uint64_t sizeRequired = 0;
    for (auto &area: options.areaSize) {
        for (uint16_t index = 0; index < options.numaNum; index++) {
            sizeRequired += area[index];
        }
    }
    struct sysinfo info{};
    if (sysinfo(&info) != 0) {
        MEM_LOG_ERROR("get sysinfo failed!");
        return false;
    }

    // 计算可用内存（空闲内存 + 缓冲区 + 缓存）
    uint64_t availableMem = info.freeram;
    availableMem += info.bufferram;
    availableMem *= info.mem_unit; // 转换为字节

    return availableMem >= sizeRequired;
}

BResult MmsMemMgr::Initialize(MemMgrOptions &options, MemLogFunc func, bool isServer)
{
    BResult ret;

    if (isServer && !CheckMemEnough(options)) {
        MEM_LOG_ERROR("memory not enough!");
        return MMS_ERR;
    }

    MemLog::Instance()->SetLogFuncFunc(func);

    for (uint16_t index = 0; index < options.numaNum; index++) {
        mNumaId[index] = options.numaId[index];
        mNumaSize[index] = options.numaSize[index];
    }
    mNumaNum = options.numaNum;

    for (uint16_t area = 0; area < MMAP_AREA_BUTT; area++) {
        mAreaFd[area] = options.areaFd[area];
        uint64_t totalSize = 0;
        for (uint16_t index = 0; index < mNumaNum; index++) {
            mAreaSize[area][index] = options.areaSize[area][index];
            totalSize += options.areaSize[area][index];
        }
        mAreaMode[area] = options.areaMode[area];
        if (mAreaFd[area] == -1) {
            std::string name = "mms_share_" + std::to_string(area);
            ret = CreateShmFdWithName(mAreaFd[area], totalSize, name);
            if (ret != MMS_OK) {
                return ret;
            }
            mIsCreated = true;
        }
        ret = CreateShmMmapAddress(mAreaFd[area], options.numaId, options.areaSize[area], mNumaNum, mAreaMode[area],
                                   mAreaAddr[area]);
        if (ret != MMS_OK) {
            return ret;
        }
    }

    return MMS_OK;
}

void MmsMemMgr::Exit(void)
{
    return;
}

BResult MmsMemMgr::GetNumaMemDesc(uint16_t numaIds[], uint64_t sizes[], uint16_t &count)
{
    uint16_t index;
    for (index = 0; index < mNumaNum; index++) {
        numaIds[index] = mNumaId[index];
        sizes[index] = mNumaSize[index];
    }
    count = mNumaNum;
    return MMS_OK;
}

BResult MmsMemMgr::GetNumaMemDesc(uint16_t numaIds[], uint16_t &count)
{
    uint16_t index;
    for (index = 0; index < mNumaNum; index++) {
        numaIds[index] = mNumaId[index];
    }
    count = mNumaNum;
    return MMS_OK;
}

BResult MmsMemMgr::GetAreaMemDesc(uint64_t addrs[], uint64_t sizes[], uint16_t &count)
{
    for (uint16_t area = 0; area < MMAP_AREA_BUTT; area++) {
        addrs[area] = mAreaAddr[area];
        sizes[area] = 0;
        for (uint16_t index = 0; index < mNumaNum; index++) {
            sizes[area] += mAreaSize[area][index];
        }
    }
    count = MMAP_AREA_BUTT;
    return MMS_OK;
}

BResult MmsMemMgr::GetAreaMemDesc(MmapArea area, int32_t &fd)
{
    fd = mAreaFd[area];
    return MMS_OK;
}

BResult MmsMemMgr::GetAreaMemDesc(MmapArea area, uint64_t &addr, uint64_t &size)
{
    uint16_t index;
    size = 0;
    for (index = 0; index < mNumaNum; index++) {
        size += mAreaSize[area][index];
    }
    addr = mAreaAddr[area];
    return MMS_OK;
}

BResult MmsMemMgr::GetAreaMemDesc(MmapArea area, uint16_t numaIds[], uint64_t sizes[], uint64_t addrs[],
                                  uint16_t &count)
{
    uint16_t index;
    uint64_t offset = 0;
    for (index = 0; index < mNumaNum; index++) {
        numaIds[index] = mNumaId[index];
        sizes[index] = mAreaSize[area][index];
        addrs[index] = mAreaAddr[area] + offset;
        offset += mAreaSize[area][index];
    }
    count = mNumaNum;
    return MMS_OK;
}

BResult MmsMemMgr::CreateShmFdWithName(int32_t &shmFd, uint64_t size, std::string &name)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
    int fd = shm_open(name.c_str(), O_CREAT | O_RDWR | O_EXCL | O_CLOEXEC, 0600UL);
#else
    int fd = syscall(SYS_memfd_create, name.c_str(), 0);
#endif
    if (fd < 0) {
        MEM_LOG_ERROR("create memory file " << name << ", failed, error:" << strerror(errno));
        return MMS_INNER_ERR;
    }

    int ret = ftruncate(fd, static_cast<off_t>(size));
    if (ret < 0) {
        MEM_LOG_ERROR("truncate file " << name << " with size " << size << " failed, error:" << strerror(errno));
        shm_unlink(name.c_str());
        close(fd);
        return MMS_INNER_ERR;
    }

    shmFd = fd;
    return MMS_OK;
}

BResult MmsMemMgr::CreateShmMmapAddress(int32_t shmFd, uint16_t numaId[], uint64_t size[], uint16_t num,
                                        MmapMode mode, uint64_t &shareAddr)
{
    int prot = 0;
    if (mode == MMAP_MODE_RW) {
        prot = PROT_READ | PROT_WRITE;
    } else if (mode == MMAP_MODE_WRITE) {
        prot = PROT_WRITE;
    } else {
        prot = PROT_READ;
    }

    uint64_t totalSize = 0;
    for (uint16_t index = 0; index < num; index++) {
        totalSize += size[index];
    }

    auto address = mmap(nullptr, totalSize, prot, MAP_SHARED, shmFd, 0);
    if (address == MAP_FAILED) {
        MEM_LOG_ERROR("Mmap shm mem, size " << totalSize << " failed, error:" << strerror(errno));
        return MMS_ERR;
    }
    shareAddr = reinterpret_cast<uint64_t>(address);

    if (NumaAvailable() == -1) {
        MEM_LOG_WARN("NUMA is not available on this system, no needed to bind numa.");
        return MMS_OK;
    }

    uint64_t offset = 0;
    for (uint16_t index = 0; index < num; index++) {
        if (BindMemoryToNuma(reinterpret_cast<int *>(shareAddr + offset), size[index], numaId[index]) < 0) {
            MEM_LOG_ERROR("Failed to bind memory to NUMA node:" << numaId[index]);
            munmap(address, totalSize);
            return MMS_ERR;
        }

        offset += size[index];
    }
    return MMS_OK;
}
}
}

