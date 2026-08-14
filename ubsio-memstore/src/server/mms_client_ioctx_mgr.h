/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of the License at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef MMS_CLIENT_IOCTX_MGR_H
#define MMS_CLIENT_IOCTX_MGR_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "mms_err.h"
#include "mms_message.h"
#include "mms_types.h"
#include "net_engine.h"

namespace ock {
namespace mms {

enum class ClientIoCtxState : uint8_t {
    CREATING = 0,
    ACTIVE,
    DRAINING
};

struct ClientIoCtx {
    ~ClientIoCtx();

    uint64_t generation = 0;
    uint32_t pid = 0;
    uint16_t numaNum = 0;
    uint16_t numaId[MAX_NUMAS_NUM] = {0};
    uint64_t numaOffset[MAX_NUMAS_NUM] = {0};
    uint64_t numaSize[MAX_NUMAS_NUM] = {0};
    uint64_t totalSize = 0;
    int32_t fd = -1;
    uintptr_t address = 0;
    MemoryRegion mr;
    MmsMemoryKey memoryKey{};
    bool mrRegistered = false;
    std::atomic<ClientIoCtxState> state{ClientIoCtxState::CREATING};
    NetEnginePtr netEngine = nullptr;
};

using ClientIoCtxPtr = std::shared_ptr<ClientIoCtx>;

struct ClientIoCtxInfo {
    uint64_t generation = 0;
    uint64_t totalSize = 0;
    uint32_t pid = 0;
    ClientIoCtxState state = ClientIoCtxState::CREATING;
};

struct ClientIoCtxStats {
    uint64_t activeCapacity = 0;
    uint64_t peakCapacity = 0;
    uint64_t createAttemptTotal = 0;
    uint64_t createdTotal = 0;
    uint64_t releasedTotal = 0;
    uint64_t createFailedTotal = 0;
    uint64_t createWaitTotal = 0;
    uint64_t createLatencyTotalUs = 0;
    uint64_t createLatencyMaxUs = 0;
    uint32_t activeClients = 0;
    uint32_t creatingClients = 0;
    uint32_t peakClients = 0;
};

class ClientIoCtxManager {
public:
    BResult Initialize(const uint16_t numaIds[], uint16_t numaNum, uint64_t sizePerNuma,
                       const NetEnginePtr &netEngine);
    BResult Acquire(uint32_t clientPid, ClientIoCtxPtr &ioCtx);
    BResult Resolve(uint32_t clientPid, uint64_t generation, uint64_t offset, uint64_t length, ClientIoCtxPtr &ioCtx,
                    uintptr_t &address) const;
    std::vector<ClientIoCtxInfo> GetInfos() const;
    ClientIoCtxStats GetStats() const;
    void Release(uint32_t clientPid);
    void Exit();

private:
    struct CreateSlot {
        std::condition_variable condition;
        ClientIoCtxPtr ioCtx;
        BResult result = MMS_NOT_READY;
        bool done = false;
    };

    BResult Create(uint32_t clientPid, ClientIoCtxPtr &ioCtx);
    static bool Contains(const ClientIoCtx &ioCtx, uint64_t offset, uint64_t length);

private:
    mutable std::mutex mMutex;
    uint16_t mNumaNum = 0;
    uint16_t mNumaId[MAX_NUMAS_NUM] = {0};
    uint64_t mSizePerNuma = 0;
    std::atomic<uint64_t> mGeneration{0};
    NetEnginePtr mNetEngine = nullptr;
    std::unordered_map<uint32_t, ClientIoCtxPtr> mClients;
    std::unordered_map<uint32_t, std::shared_ptr<CreateSlot>> mCreating;
    ClientIoCtxStats mStats;
    std::condition_variable mCreateCondition;
    bool mStopping = false;
};

}
}

#endif
