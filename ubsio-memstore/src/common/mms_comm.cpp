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

#include <sched.h>
#include <dirent.h>
#include <fstream>
#include <cstring>
#include "mms_def.h"
#include "mms_comm.h"

namespace ock {
namespace mms {

static int s_index = 0;

void NumaManager::ParseCpuList(const std::string &cpuList, std::pair<uint16_t, uint16_t> &cpuSet)
{
    std::string cpuListStr = cpuList;
    cpuListStr.erase(0, cpuListStr.find_first_not_of(" \t\n\r"));
    cpuListStr.erase(cpuListStr.find_last_not_of(" \t\n\r") + 1);

    if (cpuListStr.empty()) {
        cpuSet = {0, 0};
        return;
    }

    // 查找 '-'，判断是范围还是单个CPU
    size_t dashPos = cpuListStr.find('-');
    if (dashPos != std::string::npos) {
        // 范围
        std::string startStr = cpuListStr.substr(0, dashPos);
        std::string endStr = cpuListStr.substr(dashPos + 1);
        try {
            uint16_t start = static_cast<uint16_t>(std::stoi(startStr));
            uint16_t end = static_cast<uint16_t>(std::stoi(endStr));
            cpuSet = {start, end};
        } catch (...) {
            cpuSet = {0, 0};
        }
    } else {
        // 单个cpu
        try {
            uint16_t cpu = static_cast<uint16_t>(std::stoi(cpuListStr));
            cpuSet = {cpu, cpu};
        } catch (...) {
            cpuSet = {0, 0};
        }
    }
}

void NumaManager::InitNumaMapping()
{
    const std::string nodeDir = "/sys/devices/system/node/";
    const uint8_t nodePrefixLen = 4;
    mCpuNum = GetDeviceCpuNum();
    if (mCpuNum == 0) {
        return;
    }
    mCpuToNumaMap.assign(mCpuNum, 0);

    DIR *dir = opendir(nodeDir.c_str());
    if (!dir) {
        return;
    }

    struct dirent *entry;
    int numaNodeNum = 0;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "node", nodePrefixLen) != 0) {
            continue;
        }

        int nodeId = std::atoi(entry->d_name + nodePrefixLen);
        std::string cpuListPath = nodeDir + entry->d_name + std::string("/cpulist");
        char *canonicalPath = realpath(cpuListPath.c_str(), nullptr);
        if (canonicalPath == nullptr) {
            continue;
        }

        std::ifstream infile(canonicalPath);
        free(canonicalPath);
        if (!infile.is_open()) {
            continue;
        }

        std::string cpuList;
        std::pair<uint16_t, uint16_t> cpuSet;
        if (!std::getline(infile, cpuList)) {
            infile.close();
            continue;
        }
        infile.close();

        ParseCpuList(cpuList, cpuSet);
        for (uint16_t cpu = cpuSet.first; cpu <= cpuSet.second; ++cpu) {
            mCpuToNumaMap[cpu] = nodeId;
        }

        ++numaNodeNum;
    }

    closedir(dir);
    mNumaNum = numaNodeNum;
}

int NumaManager::BindMemoryToNuma(void *addr, size_t memSize, uint16_t numaId)
{
    if (addr == nullptr || static_cast<int>(numaId) >= GetNumaNodeNum()) {
        return -1;
    }

    unsigned long nodeMask = 0;
    nodeMask |= (1UL << numaId);
    unsigned long maskBits = sizeof(nodeMask) * NO_8;
    long ret = syscall(SYS_mbind, addr, memSize, MPOL_BIND, &nodeMask, maskBits, MPOL_MF_MOVE);
    if (UNLIKELY(ret < 0)) {
        return -1;
    }

    return 0;
}

class ThreadIndex {
public:
    ThreadIndex()
    {
        uint16_t numaId = GetCurCPUNumaNode();
        uint16_t numaIndex = NumaGroupIndex::Instance()->GetNumaIndex(numaId);
        uint16_t numaNum = NumaGroupIndex::Instance()->GetNumaNum();
        uint16_t groupNum = NumaGroupIndex::Instance()->GetGroupNum();

        if (numaIndex == numaNum) {
            int value = __sync_fetch_and_add(&s_index, 1);
            mThreadIndex = value % groupNum;
            return;
        }

        if ((numaNum != 0) && (groupNum != 0) && (groupNum % numaNum == 0)) {
            uint16_t base = groupNum / numaNum;
            int value = __sync_fetch_and_add(&s_index, 1);
            mThreadIndex = (numaIndex * base) + (value % base);
            return;
        }
    }

    ~ThreadIndex() = default;

    inline uint16_t GetTheadIndex() const
    {
        return mThreadIndex;
    }

private:
    uint16_t mThreadIndex = 0;
};

static thread_local ThreadIndex s_threadIndex;

uint16_t NumaGroupIndex::GetGroupIndex() const
{
    return s_threadIndex.GetTheadIndex();
}
}
}

