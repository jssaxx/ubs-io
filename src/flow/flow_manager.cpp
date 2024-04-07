/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */
#include "flow_manager.h"
#include "flow_id_allocator.h"
#include "bio_types.h"
#include "bio_config_instance.h"
#include "bio_trace.h"
#include "bdm_core.h"

namespace ock {
namespace bio {
MemAllocator FlowManager::mMemAllocator;
DiskAllocator FlowManager::mDiskAllocator;
BResult FlowManager::Init()
{
    if (mInited) {
        return BIO_OK;
    }
    mTaskPool = MakeRef<FlowTaskPool>("flow");
    if (mTaskPool == nullptr) {
        LOG_ERROR("Failed to start flow task pool, probably out of memory");
        return BIO_ERR;
    }
    auto ret = mTaskPool->Start(NO_4, NO_4096);
    if (ret != BIO_OK) {
        return ret;
    }

    mInited = true;

    ret = Recover();
    if (ret != BIO_OK) {
        return ret;
    }

    return BIO_OK;
}

BResult FlowManager::Exit()
{
    mTaskPool->Stop();
    mInited = false;
    return BIO_OK;
}

FlowPtr FlowManager::CreateObject(FlowType type, uint64_t flowId, uint32_t mediaId)
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mInited) {
        LOG_WARN("Flow manager not ready.");
        return nullptr;
    }
    BioConfigPtr config = BioConfig::Instance();
    uint64_t segment = config->GetDaemonConfig().segment;
    if (type == FLOW_MEMORY) {
        auto it = mMemObjManager.find(flowId);
        if (it != mMemObjManager.end()) {
            return it->second;
        }
        BIO_TRACE_START(FLOW_TRACE_CREATE_OBJ);
        auto object = MakeRef<Flow>(type, flowId, mediaId, segment, segment * NO_4);
        ChkTrueNot(object != nullptr, nullptr);
        mMemObjManager[flowId] = object;
        BIO_TRACE_END(FLOW_TRACE_CREATE_OBJ, 0);
        return object;
    } else {
        auto it = mDiskObjManager.find(flowId);
        if (it != mDiskObjManager.end()) {
            return it->second;
        }
        BIO_TRACE_START(FLOW_TRACE_CREATE_OBJ);
        auto object = MakeRef<Flow>(type, flowId, mediaId, segment, segment * NO_16);
        ChkTrueNot(object != nullptr, nullptr);
        mDiskObjManager[flowId] = object;
        BIO_TRACE_END(FLOW_TRACE_CREATE_OBJ, 0);
        return object;
    }
}

FlowPtr FlowManager::GetObject(FlowType type, uint64_t flowId)
{
    std::lock_guard<std::mutex> lock(mMutex);
    ChkTrueNot(mInited, nullptr);
    if (type == FLOW_MEMORY) {
        auto it = mMemObjManager.find(flowId);
        if (it != mMemObjManager.end()) {
            return it->second;
        }
        return nullptr;
    } else {
        auto it = mDiskObjManager.find(flowId);
        if (it != mDiskObjManager.end()) {
            return it->second;
        }
        return nullptr;
    }
}

BResult FlowManager::DestroyObject(FlowType type, uint64_t flowId)
{
    std::lock_guard<std::mutex> lock(mMutex);
    ChkTrueNot(mInited == true, BIO_NOT_READY);
    if (type == FLOW_MEMORY) {
        auto it = mMemObjManager.find(flowId);
        if (it != mMemObjManager.end()) {
            BIO_TRACE_START(FLOW_TRACE_DESTROY_OBJ);
            mMemObjManager.erase(flowId);
            BIO_TRACE_END(FLOW_TRACE_DESTROY_OBJ, 0);
            return BIO_OK;
        }
        return BIO_NOT_EXISTS;
    } else {
        auto it = mDiskObjManager.find(flowId);
        if (it != mDiskObjManager.end()) {
            BIO_TRACE_START(FLOW_TRACE_DESTROY_OBJ);
            mDiskObjManager.erase(flowId);
            BIO_TRACE_END(FLOW_TRACE_DESTROY_OBJ, 0);
            return BIO_OK;
        }
        return BIO_NOT_EXISTS;
    }
}

BResult FlowManager::GetAllObject(FlowType type, std::map<uint64_t, FlowPtr> &objManager)
{
    std::lock_guard<std::mutex> lock(mMutex);
    ChkTrueNot(mInited == true, BIO_NOT_READY);
    BIO_TRACE_START(FLOW_TRACE_GETALL_OBJ);
    if (type == FLOW_MEMORY) {
        for (auto it = mMemObjManager.begin(); it != mMemObjManager.end(); ++it) {
            objManager[it->first] = it->second;
        }
    } else {
        for (auto it = mDiskObjManager.begin(); it != mDiskObjManager.end(); ++it) {
            objManager[it->first] = it->second;
        }
    }
    BIO_TRACE_END(FLOW_TRACE_GETALL_OBJ, 0);
    return BIO_OK;
}

BResult FlowManager::PreLoadObject(std::function<void()> handle)
{
    std::lock_guard<std::mutex> lock(mMutex);
    ChkTrueNot(mTaskPool != nullptr, BIO_NOT_READY);
    BIO_TRACE_START(FLOW_TRACE_PRELOAD_OBJ);
    auto ret = mTaskPool->AddTask(handle);
    BIO_TRACE_END(FLOW_TRACE_PRELOAD_OBJ, ret);
    if (ret != BIO_OK) {
        LOG_ERROR("Submit task failed:" << ret);
        return ret;
    }

    return BIO_OK;
}

BResult FlowManager::Recover()
{
    BioConfigPtr config = BioConfig::Instance();
    uint32_t diskNum = config->GetDaemonConfig().diskList.size();
    uint64_t segment = config->GetDaemonConfig().segment;

    uint64_t chunkId;
    uint64_t chunkSize;
    uint64_t flowId;
    uint64_t flowOffset;
    int ret;

    for (uint32_t diskId = 0; diskId < diskNum; diskId++) {
        if (BdmGetDiskStatus(diskId) == BDM_DISK_STATE_FAULT) {
            LOG_ERROR("Bdm get diskId:" << diskId << " status is not ok.");
            continue;
        }
        ret = BdmResetScanPool(diskId);
        if (ret != BDM_CODE_OK) {
            LOG_ERROR("Bdm reset scan fail:" << ret << ", diskId:" << diskId);
            return BIO_ERR;
        }
        while (true) {
            ret = BdmGetNextUsedChunkId(diskId, &chunkId, &chunkSize, &flowId, &flowOffset);
            if (ret != BDM_CODE_OK && ret != BDM_CODE_SCAN_OFF) {
                LOG_ERROR("Bdm invalid diskId:" << diskId << ", ret:" << ret);
                return BIO_ERR;
            }
            if (ret == BDM_CODE_SCAN_OFF) {
                break;
            }
            if (segment != chunkSize) {
                LOG_ERROR("Bdm check chunk fail:" << chunkSize << ", config:" << segment);
                return BIO_ERR;
            }
            ret = RecoverChunk(diskId, chunkId, flowId, flowOffset);
            if (ret != BDM_CODE_OK) {
                LOG_ERROR("Bdm rebuild chunk diskId:" << diskId << ", ret:" << ret);
                return BIO_ERR;
            }
        }
    }

    for (auto it = mDiskObjManager.begin(); it != mDiskObjManager.end(); ++it) {
        ret = it->second->RecoverCheck();
        if (ret != BIO_OK) {
            LOG_ERROR("Check flow fail:" << flowId << ", offset:" << flowOffset << ", ret:" << ret);
            return BIO_ERR;
        }
    }

    return BIO_OK;
}

BResult FlowManager::RecoverChunk(uint32_t mediaId, uint64_t chunkId, uint64_t flowId, uint64_t flowOffset)
{
    FlowPtr flow = CreateObject(FLOW_DISK, flowId, mediaId);
    if (flow == nullptr) {
        LOG_ERROR("Get flow fail:" << flowId);
        return BIO_ERR;
    }

    BResult ret = flow->RecoverChunk(flowOffset, chunkId);
    if (ret != BIO_OK) {
        LOG_ERROR("Recover flow fail:" << flowId << ", offset:" << flowOffset << ", chunk:" << chunkId);
        return BIO_ERR;
    }

    auto idAllocator = FlowIdAllocator::Instance();
    idAllocator->SyncFlowId(flowId);

    return BIO_OK;
}
}
}
