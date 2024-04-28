/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#include "cm.h"
#include "bio_log.h"
#include "cache_flow.h"
#include "flow_id_allocator.h"
#include "flow_manager.h"
#include "rcache_flow.h"

using namespace ock::bio;

RCacheFlow::RCacheFlow() : mMetaFlow(nullptr), mDataFlow(nullptr) {}

RCacheFlow::~RCacheFlow()
{
    mMetaFlow = nullptr;
    mDataFlow = nullptr;
    mMetaFlowInstance = nullptr;
    mDataFlowInstance = nullptr;
}

FlowPtr &RCacheFlow::GetMetaFlow()
{
    return mMetaFlow;
}

FlowPtr &RCacheFlow::GetDataFlow()
{
    return mDataFlow;
}

FlowInstancePtr &RCacheFlow::GetMetaFlowInstance()
{
    return mMetaFlowInstance;
}

FlowInstancePtr &RCacheFlow::GetDataFlowInstance()
{
    return mDataFlowInstance;
}

BResult RCacheFlow::AllocOffset(uint64_t len, uint64_t &offset, uint64_t &indexInFlow)
{
    WriteLocker<ReadWriteLock> lock(&mLock);
    offset = mDataFlowInstance->AllocOffset(len, indexInFlow);

    std::vector<FlowAddr> addr;
    BResult ret = mDataFlow->GetAddrByOffset(offset, len, addr);
    if (UNLIKELY(ret != BIO_OK)) {
        mDataFlowInstance->RollbackOffset(len);
        return ret;
    }
    return BIO_OK;
}

BResult RCacheFlow::Initialize(uint64_t ptId, uint16_t diskId, FlowType flowType, std::vector<uint64_t> flowIds)
{
    mPtId = ptId;
    mDiskId = diskId;

    if (UNLIKELY(flowIds.empty())) {
        LOG_ERROR("Generate pt id " << ptId << "flow ids failed.");
        return BIO_ERR;
    }

    mMetaFlow = FlowManager::Instance()->CreateObject(flowType, flowIds[0], diskId);
    if (UNLIKELY(mMetaFlow == nullptr)) {
        LOG_ERROR("Create pt id " << ptId << " flow type " << flowType << " meta flow failed.");
        Destroy();
        return BIO_ERR;
    }

    LOG_INFO("Meta flowId:" << mMetaFlow->GetFlowId() << ", flowType:" << flowType);

    mMetaFlowInstance = MakeRef<FlowInstance>(flowIds[0]);
    if (UNLIKELY(mMetaFlowInstance == nullptr)) {
        LOG_ERROR("Create pt id " << ptId << " flow type " << flowType << " flow instance failed.");
        Destroy();
        return BIO_ERR;
    }

    mDataFlow = FlowManager::Instance()->CreateObject(flowType, flowIds[1], diskId);
    if (UNLIKELY(mDataFlow == nullptr)) {
        LOG_ERROR("Create pt id " << ptId << " flow type " << flowType << " data flow failed.");
        Destroy();
        return BIO_ERR;
    }

    LOG_INFO("Data flowId:" << mDataFlow->GetFlowId() << ", flowType:" << flowType);

    mDataFlowInstance = MakeRef<FlowInstance>(flowIds[1]);
    if (UNLIKELY(mDataFlowInstance == nullptr)) {
        LOG_ERROR("Create pt id " << ptId << " flow type " << flowType << " flow instance failed.");
        Destroy();
        return BIO_ERR;
    }

    return BIO_OK;
}

BResult RCacheFlow::Destroy()
{
    if (mMetaFlow != nullptr) {
        mMetaFlow->Seal();
        FlowManager::Instance()->DestroyObject(mMetaFlow->GetFlowType(), mMetaFlow->GetFlowId());
        mMetaFlow = nullptr;
    }

    if (mDataFlow != nullptr) {
        mDataFlow->Seal();
        FlowManager::Instance()->DestroyObject(mDataFlow->GetFlowType(), mDataFlow->GetFlowId());
        mDataFlow = nullptr;
    }

    return BIO_OK;
}
