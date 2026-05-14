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

#include "mms_trace.h"
#include "mms_types.h"
#include "mms_kv_server.h"
#include "mms_server.h"
#include "mms_log.h"
#include "mms_conv.h"

namespace ock {
namespace mms {
static MmsKvServerPtr gServer = MmsKvServer::Instance();
static thread_local bool isSeparateMode = MmsServer::Instance()->GetConfig()->GetBasicConfig().isSeparateMode;

inline static CResult ToCResult(const BResult ret)
{
    switch (ret) {
        case MMS_OK:
            return RET_MMS_OK;
        case MMS_ERR:
        case MMS_INNER_ERR:
            return RET_MMS_ERROR;
        case MMS_NOT_READY:
            return RET_MMS_UNAVAILABLE;
        case MMS_INVALID_PARAM:
            return RET_MMS_EPERM;
        case MMS_ALLOC_FAIL:
            return RET_MMS_NEED_RETRY;
        case MMS_NOT_INITIALIZED:
            return RET_MMS_NOT_READY;
        case MMS_NOT_EXISTS:
            return RET_MMS_NOT_FOUND;
        case MMS_CHECK_PT_FAIL:
            return RET_MMS_PT_FAULT;
        case MMS_READ_EXCEED:
            return RET_MMS_READ_EXCEED;
        case MMS_KEY_CONFLICT:
            return RET_MMS_CONFLICT;
        default:
            return RET_MMS_NEED_RETRY;
    }
}

inline static bool KeyValid(const char *key)
{
    if (UNLIKELY(key == nullptr || strlen(key) == 0 || strlen(key) >= MAX_KEY_SIZE)) {
        return false;
    }
    return true;
}

inline static bool RangeValid(const char *start, const char *end)
{
    return KeyValid(start) && KeyValid(end) && strcmp(start, end) <= 0;
}

CResult MmsConv::Initialize(const MmsOptions &options, ServiceCallback service)
{
    auto mmsServer = MmsServer::Instance();
    auto ret = mmsServer->Start(service);
    return ToCResult(ret);
}

void MmsConv::Exit()
{
    auto mmsServer = MmsServer::Instance();
    mmsServer->Exit();
}

CResult MmsConv::Put(uint64_t userId, PutItems *itemList, uint32_t itemNum)
{
    MMS_TRACE_START(SDK_TRACE_PUT);
    if (isSeparateMode) {
        return RET_MMS_PROTECTED;
    }

    if (UNLIKELY(itemList == nullptr || itemNum == 0)) {
        return RET_MMS_EPERM;
    }

    for (uint32_t i = 0; i < itemNum; i++) {
        if (UNLIKELY(!KeyValid(itemList[i].key) || itemList[i].value == nullptr || itemList[i].length == 0)) {
            return RET_MMS_EPERM;
        }
    }

    auto ret = ToCResult(gServer->Put(userId, itemList, itemNum));
    MMS_TRACE_END(SDK_TRACE_PUT, ret);
    return ret;
}

CResult MmsConv::Get(uint64_t userId, GetItems *itemList, uint32_t itemNum)
{
    MMS_TRACE_START(SDK_TRACE_GET);
    if (isSeparateMode) {
        return RET_MMS_PROTECTED;
    }

    if (UNLIKELY(itemList == nullptr || itemNum == 0)) {
        return RET_MMS_EPERM;
    }

    for (uint32_t i = 0; i < itemNum; i++) {
        if (UNLIKELY(!KeyValid(itemList[i].key) || itemList[i].length == 0 || itemList[i].value == nullptr ||
                     itemList[i].realLength == nullptr)) {
            return RET_MMS_EPERM;
        }
    }

    auto ret = ToCResult(gServer->Get(userId, itemList, itemNum));
    MMS_TRACE_END(SDK_TRACE_GET, ret);
    return ret;
}

CResult MmsConv::GetValuesByPrefix(const char *prefix, ValueInfo **valueInfoItems, uint64_t *itemNum)
{
    if (isSeparateMode) {
        return RET_MMS_PROTECTED;
    }

    if (UNLIKELY(!KeyValid(prefix) || valueInfoItems == nullptr || itemNum == nullptr)) {
        return RET_MMS_EPERM;
    }

    return ToCResult(gServer->GetValuesByPrefix(prefix, valueInfoItems, itemNum));
}

CResult MmsConv::GetValuesByRange(const char *start, const char *end, ValueInfo **valueInfoItems, uint64_t *itemNum)
{
    if (isSeparateMode) {
        return RET_MMS_PROTECTED;
    }

    if (UNLIKELY(!RangeValid(start, end) || valueInfoItems == nullptr || itemNum == nullptr)) {
        return RET_MMS_EPERM;
    }

    return ToCResult(gServer->GetValuesByRange(start, end, valueInfoItems, itemNum));
}

CResult MmsConv::BatchDeleteByRange(const char *start, const char *end)
{
    MMS_TRACE_START(SDK_TRACE_DELETE);
    if (isSeparateMode) {
        return RET_MMS_PROTECTED;
    }

    if (UNLIKELY(!RangeValid(start, end))) {
        return RET_MMS_EPERM;
    }

    CResult ret = ToCResult(gServer->BatchDeleteByRange(start, end));
    MMS_TRACE_END(SDK_TRACE_DELETE, ret);
    return ret;
}

void MmsConv::FreeResources(ValueInfo **valueInfoItems, uint64_t itemNum)
{
    if (isSeparateMode) {
        return;
    }

    if (valueInfoItems == nullptr || *valueInfoItems == nullptr || itemNum == 0) {
        return;
    }

    gServer->FreeResources(valueInfoItems, itemNum);
}

CResult MmsConv::Update(uint64_t userId, UpdateItems *itemList, uint32_t itemNum)
{
    MMS_TRACE_START(SDK_TRACE_UPDATE);
    if (isSeparateMode) {
        return RET_MMS_PROTECTED;
    }
    if (UNLIKELY(itemList == nullptr || itemNum == 0)) {
        return RET_MMS_EPERM;
    }

    for (uint32_t i = 0; i < itemNum; i++) {
        if (UNLIKELY(!KeyValid(itemList[i].key) || itemList[i].value == nullptr || itemList[i].length == 0)) {
            return RET_MMS_EPERM;
        }
    }

    auto ret = ToCResult(gServer->Update(userId, itemList, itemNum));
    MMS_TRACE_END(SDK_TRACE_UPDATE, ret);
    return ret;
}

CResult MmsConv::Delete(uint64_t userId, DeleteItems *itemList, uint32_t itemNum)
{
    MMS_TRACE_START(SDK_TRACE_DELETE);
    if (isSeparateMode) {
        return RET_MMS_PROTECTED;
    }
    if (UNLIKELY(itemList == nullptr || itemNum == 0)) {
        return RET_MMS_EPERM;
    }
    for (uint32_t i = 0; i < itemNum; i++) {
        if (!KeyValid(itemList[i].key)) {
            return RET_MMS_EPERM;
        }
    }
    auto ret = ToCResult(gServer->Delete(userId, itemList, itemNum));
    MMS_TRACE_END(SDK_TRACE_DELETE, ret);
    return ret;
}

CResult MmsConv::Replace(uint64_t userId, ReplaceItems *itemList, uint32_t itemNum)
{
    MMS_TRACE_START(SDK_TRACE_REPLACE);
    if (isSeparateMode) {
        return RET_MMS_PROTECTED;
    }

    if (UNLIKELY(itemList == nullptr || itemNum == 0)) {
        return RET_MMS_EPERM;
    }

    for (uint32_t i = 0; i < itemNum; i++) {
        if (UNLIKELY(!KeyValid(itemList[i].key) || itemList[i].value == nullptr || itemList[i].length == 0)) {
            return RET_MMS_EPERM;
        }
    }

    auto ret = ToCResult(gServer->Replace(userId, itemList, itemNum));
    MMS_TRACE_END(SDK_TRACE_REPLACE, ret);
    return ret;
}

CResult MmsConv::StartCatchUpTask()
{
    MMS_TRACE_START(SDK_TRACE_CATCH_UP);
    auto mmsServer = MmsServer::Instance();
    CResult ret = ToCResult(mmsServer->GetCrbScheduler()->StartCatchUp());
    MMS_TRACE_END(SDK_TRACE_CATCH_UP, ret)
    return ret;
}
}
}

CResult MmsInitialize(MmsOptions &options, ServiceCallback service)
{
    return ock::mms::MmsConv::Initialize(options, service);
}

void MmsExit()
{
    ock::mms::MmsConv::Exit();
}

CResult MmsPut(uint64_t userId, PutItems *itemList, uint32_t itemNum)
{
    return ock::mms::MmsConv::Put(userId, itemList, itemNum);
}

CResult MmsGet(uint64_t userId, GetItems *itemList, uint32_t itemNum)
{
    return ock::mms::MmsConv::Get(userId, itemList, itemNum);
}

CResult MmsGetValuesByPrefix(const char *prefix, ValueInfo **valueInfoItems, uint64_t *itemNum)
{
    return ock::mms::MmsConv::GetValuesByPrefix(prefix, valueInfoItems, itemNum);
}

CResult MmsGetValuesByRange(const char *start, const char *end, ValueInfo **valueInfoItems, uint64_t *itemNum)
{
    return ock::mms::MmsConv::GetValuesByRange(start, end, valueInfoItems, itemNum);
}

void MmsFreeResources(ValueInfo **valueInfoItems, uint64_t itemNum)
{
    return ock::mms::MmsConv::FreeResources(valueInfoItems, itemNum);
}

CResult MmsUpdate(uint64_t userId, UpdateItems *itemList, uint32_t itemNum)
{
    return ock::mms::MmsConv::Update(userId, itemList, itemNum);
}

CResult MmsDelete(uint64_t userId, DeleteItems *itemList, uint32_t itemNum)
{
    return ock::mms::MmsConv::Delete(userId, itemList, itemNum);
}

CResult MmsBatchDeleteByRange(const char *start, const char *end)
{
    return ock::mms::MmsConv::BatchDeleteByRange(start, end);
}

CResult MmsReplace(uint64_t userId, ReplaceItems *itemList, uint32_t itemNum)
{
    return ock::mms::MmsConv::Replace(userId, itemList, itemNum);
}

CResult MmsStartCatchUpTask()
{
    return ock::mms::MmsConv::StartCatchUpTask();
}

