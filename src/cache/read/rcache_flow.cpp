/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#include "bio_log.h"
#include "rcache_flow.h"
#include "cache_flow.h"
#include "flow_id_allocator.h"
#include "flow_manager.h"

using namespace ock::bio;

RCacheFlow::RCacheFlow():mMetaFlow(nullptr),mDataFlow(nullptr)
{
}

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

BResult RCacheFlow::Initialize(uint64_t ptId, FlowType flowType, std::vector<uint64_t> flowIds)
{
    mPtId = ptId;

    if(flowIds.empty()) {
        LOG_ERROR("Generate pt id" << ptId << "flow ids failed.");
        return BIO_ERR;
    }

    mMetaFlow  = FlowManager::Instance()->CreateObject(flowType, flowIds[0]);
    if (mMetaFlow == nullptr) {
        LOG_ERROR("Create pt id" << ptId << "flow type" << flowType << "meta flow failed.");
        Destroy();
        return BIO_ERR;
    }

    mMetaFlowInstance = MakeRef<FlowInstance>(flowIds[0]);
    if (mMetaFlowInstance == nullptr) {
        LOG_ERROR("Create pt id" << ptId << "flow type" << flowType << "flow instance failed.");
        Destroy();
        return BIO_ERR;
    }

    mDataFlow  = FlowManager::Instance()->CreateObject(flowType, flowIds[1]);
    if (mDataFlow == nullptr) {
        LOG_ERROR("Create pt id" << ptId << "flow type" << flowType << "data flow failed.");
        Destroy();
        return BIO_ERR;
    }

    mDataFlowInstance = MakeRef<FlowInstance>(flowIds[1]);
    if (mDataFlowInstance == nullptr) {
        LOG_ERROR("Create pt id" << ptId << "flow type" << flowType << "flow instance failed.");
        Destroy();
        return BIO_ERR;
    }

    return BIO_OK;
}

BResult RCacheFlow::Destroy()
{
    if (mMetaFlow != nullptr) {
        FlowManager::Instance()->DestroyObject(mMetaFlow->GetFlowType(), mMetaFlow->GetFlowId());
        mMetaFlow = nullptr;
    }

    if (mDataFlow != nullptr) {
        FlowManager::Instance()->DestroyObject(mDataFlow->GetFlowType(), mDataFlow->GetFlowId());
        mDataFlow = nullptr;
    }

    return BIO_OK;
}
