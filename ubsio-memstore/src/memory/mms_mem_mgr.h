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

#ifndef MMS_MEM_MGR_H
#define MMS_MEM_MGR_H

#include <atomic>
#include <unordered_map>
#include "mms_ref.h"
#include "mms_lock.h"
#include "mms_err.h"
#include "mms.h"
#include "mms_mem_common.h"
#include "mms_mem_log.h"

namespace ock {
namespace mms {

struct ValueIndexMemCfg {
    // 输入
    uint64_t totalMemSize;
    uint64_t indexNodeSize;
    uint64_t valueBlockSize;

    // 输出
    uint64_t indexMemSize;
    uint64_t valueMemSize;

    // 根据value buddy最小粒度算出index和value的总内存大小，使index block数量近似等于value基础block数量
    void Calculate();
};

class MmsMemMgr;
using MmsMemMgrPtr = Ref<MmsMemMgr>;
class MmsMemMgr {
public:
    MmsMemMgr() = default;
    ~MmsMemMgr() = default;

    static MmsMemMgrPtr &Instance()
    {
        static auto instance = MakeRef<MmsMemMgr>();
        return instance;
    }

    void ResetLogLevel(int32_t level)
    {
        MemLog::Instance()->SetMinLogLevel(level);
    }

    BResult Initialize(MemMgrOptions &options, MemLogFunc func, bool isServer);
    void Exit(void);

    inline BResult Reset(void)
    {
        for (uint16_t &numaId: mNumaId) {
            numaId = -1;
        }
        for (auto &fd: mAreaFd) {
            close(fd);
            fd = -1;
        }
        return MMS_OK;
    }

    BResult GetNumaMemDesc(uint16_t numaIds[], uint64_t sizes[], uint16_t &count);

    BResult GetNumaMemDesc(uint16_t numaIds[], uint16_t &count);

    BResult GetAreaMemDesc(uint64_t addrs[], uint64_t sizes[], uint16_t &count);

    BResult GetAreaMemDesc(MmapArea area, int32_t &fd);

    BResult GetAreaMemDesc(MmapArea area, uint64_t &addr, uint64_t &size);

    BResult GetAreaMemDesc(MmapArea area, uint16_t numaIds[], uint64_t sizes[], uint64_t addrs[], uint16_t &count);

    void Trans2Offset(MmapArea area, uintptr_t addr, size_t &offset)
    {
        offset = addr - mAreaAddr[area];
    }

    void Trans2Addr(MmapArea area, size_t offset, uintptr_t &addr)
    {
        addr = mAreaAddr[area] + offset;
    }

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    BResult CreateShmFdWithName(int32_t &shmFd, uint64_t size, std::string &name);
    BResult CreateShmMmapAddress(int32_t shmFd, uint16_t numaId[], uint64_t size[], uint16_t num, MmapMode mode,
                                 uint64_t &shareAddr);

private:

    uint16_t mNumaNum;
    uint16_t mNumaId[MAX_NUMAS_NUM];
    uint64_t mNumaSize[MAX_NUMAS_NUM];

    int32_t mAreaFd[MMAP_AREA_BUTT];

    uint64_t mAreaSize[MMAP_AREA_BUTT][MAX_NUMAS_NUM];

    uint64_t mAreaAddr[MMAP_AREA_BUTT];

    MmapMode mAreaMode[MMAP_AREA_BUTT];

    bool mIsCreated = false;

    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif
