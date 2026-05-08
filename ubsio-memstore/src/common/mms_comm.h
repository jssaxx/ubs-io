/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef MMS_COMM_H
#define MMS_COMM_H

#include <sys/syscall.h>
#include <errno.h>
#include <unistd.h>
#include <vector>
#include <sched.h>
#include "mms_ref.h"
#include "mms_types.h"

namespace ock {
namespace mms {

#define MPOL_BIND 2
#define MPOL_MF_MOVE (1 << 1)

class NumaManager {
public:
    static NumaManager &Instance()
    {
        static NumaManager instance;
        return instance;
    }

    inline uint16_t GetCurCPUNumaNode() const
    {
        int cpu = sched_getcpu();
        if (cpu < 0 || cpu >= static_cast<int>(mCpuNum)) {
            return 0;
        }

        return mCpuToNumaMap[cpu];
    }

    inline int GetNumaNodeNum() const
    {
        return mNumaNum;
    }

    inline uint32_t GetCpuNum() const
    {
        return mCpuNum;
    }

    inline int NumaAvailable()
    {
        long ret = syscall(SYS_get_mempolicy, nullptr, nullptr, 0UL, 0UL, 0UL);
        if (ret < 0 && (errno == ENOSYS || errno == EPERM)) {
            return -1;
        }

        return 0;
    }

    int BindMemoryToNuma(void *addr, size_t memSize, uint16_t numaId);

    inline uint64_t GetPageSize() const
    {
        long int pageSize = sysconf(_SC_PAGESIZE);
        if (pageSize <= 0) {
            return NO_4096;
        }

        return static_cast<uint64_t>(pageSize);
    }

private:
    NumaManager()
    {
        InitNumaMapping();
    };

    long GetDeviceCpuNum() const
    {
        long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
        if (cpu_count > 0) {
            return cpu_count;
        }

        return 0;
    }

    void ParseCpuList(const std::string &cpuList, std::pair<uint16_t, uint16_t> &cpuSet);

    void InitNumaMapping();

    NumaManager(const NumaManager &) = delete;
    NumaManager &operator=(const NumaManager &) = delete;

private:
    std::vector<uint16_t> mCpuToNumaMap{};
    int mNumaNum = 0;
    uint32_t mCpuNum = 0;
};

inline uint16_t GetCurCPUNumaNode()
{
    return NumaManager::Instance().GetCurCPUNumaNode();
}

inline int NumaAvailable()
{
    return NumaManager::Instance().NumaAvailable();
}

inline int GetNumaNodeNum()
{
    return NumaManager::Instance().GetNumaNodeNum();
}

inline uint32_t GetDeviceCpuNum()
{
    return NumaManager::Instance().GetCpuNum();
}

inline int BindMemoryToNuma(void *addr, size_t memSize, uint16_t numaId)
{
    return NumaManager::Instance().BindMemoryToNuma(addr, memSize, numaId);
}

inline uint64_t GetDevicePageSize()
{
    return NumaManager::Instance().GetPageSize();
}

class NumaGroupIndex;
using NumaGroupIndexPtr = Ref<NumaGroupIndex>;
class NumaGroupIndex {
public:
    inline static NumaGroupIndexPtr &Instance()
    {
        static auto instance = MakeRef<NumaGroupIndex>();
        return instance;
    }

    void SetNumaInfo(uint16_t numaId[], uint16_t numaNum)
    {
        if (numaNum > MAX_NUMAS_NUM) {
            return;
        }
        for (uint16_t index = 0; index < numaNum; index++) {
            mNumaId[index] = numaId[index];
        }
        mNumaNum = numaNum;
    }

    void SetGroupInfo(uint16_t groupNum)
    {
        mGroupNum = groupNum;
    }

    uint16_t GetNumaIndex(uint16_t numaId)
    {
        for (uint16_t index = 0; index < mNumaNum; index++) {
            if (mNumaId[index] == numaId) {
                return index;
            }
        }
        return mNumaNum;
    }

    uint16_t GetNumaNum() const
    {
        return mNumaNum;
    }

    uint16_t GetGroupNum() const
    {
        return mGroupNum;
    }

    uint16_t GetGroupIndex() const;

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    uint16_t mNumaId[MAX_NUMAS_NUM];
    uint16_t mNumaNum = 0;
    uint16_t mGroupNum = 0;

    DEFINE_REF_COUNT_VARIABLE;
};
}
}

#endif  // MMS_COMM_H

