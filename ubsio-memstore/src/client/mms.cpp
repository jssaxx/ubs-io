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

#include "mms_client_log.h"
#include "mms_client.h"
#include "mms_trace.h"
#include "mms_types.h"
#include "mms.h"

namespace ock {
namespace mms {
static MmsClientPtr gClient = MmsClient::Instance();

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

inline static bool KeyValid(const char *key, uint16_t keyLen)
{
    if (UNLIKELY(key == nullptr || keyLen == 0 || keyLen >= MAX_KEY_SIZE)) {
        return false;
    }
    return true;
}

inline static bool KeyValid(const char *key)
{
    return key != nullptr && strlen(key) != 0 && strlen(key) < MAX_KEY_SIZE;
}

inline static bool RangeValid(const char *start, const char *end)
{
    return KeyValid(start) && KeyValid(end) && strcmp(start, end) <= 0;
}

CResult Mms::Initialize(const MmsOptions &options, ServiceCallback service)
{
    if (UNLIKELY(gClient == nullptr)) {
        CLIENT_LOG_ERROR("Get client instance failed.");
        return RET_MMS_ERROR;
    }
    return ToCResult(gClient->Initialize(options, service));
}

CResult Mms::RegisterCallback(NotifyCallback callback)
{
    if (UNLIKELY(gClient == nullptr)) {
        CLIENT_LOG_ERROR("Get client instance failed.");
        return RET_MMS_ERROR;
    }
    return ToCResult(gClient->RegisterNotifyCallback(callback));
}

void Mms::Exit()
{
    gClient->Exit();
}

CResult Mms::Put(PutItems *itemList, uint32_t itemNum)
{
    MMS_TRACE_START(SDK_TRACE_PUT);
    if (UNLIKELY(itemList == nullptr || itemNum == 0)) {
        return RET_MMS_EPERM;
    }

    for (uint32_t i = 0; i < itemNum; i++) {
        if (UNLIKELY(!KeyValid(itemList[i].key, itemList[i].keyLen) || itemList[i].value == nullptr ||
                     itemList[i].valueLen == 0 || itemList[i].valueAddr == nullptr ||
                     itemList[i].result == nullptr)) {
            return RET_MMS_EPERM;
        }
        *itemList[i].result = static_cast<int32_t>(MMS_OK);
    }

    auto ret = ToCResult(gClient->MmsPut(itemList, itemNum));
    for (uint32_t i = 0; i < itemNum; i++) {
        *itemList[i].result = static_cast<int32_t>(ToCResult(static_cast<BResult>(*itemList[i].result)));
    }
    MMS_TRACE_END(SDK_TRACE_PUT, ret);
    return ret;
}

CResult Mms::Get(GetItems *itemList, uint32_t itemNum)
{
    MMS_TRACE_START(SDK_TRACE_GET);
    if (UNLIKELY(itemList == nullptr || itemNum == 0)) {
        return RET_MMS_EPERM;
    }

    for (uint32_t i = 0; i < itemNum; i++) {
        if (UNLIKELY(!KeyValid(itemList[i].key, itemList[i].keyLen) || itemList[i].length == 0 ||
                     itemList[i].value == nullptr || itemList[i].realLength == nullptr ||
                     itemList[i].result == nullptr)) {
            return RET_MMS_EPERM;
        }
        *itemList[i].result = static_cast<int32_t>(MMS_OK);
    }

    auto ret = ToCResult(gClient->MmsGet(itemList, itemNum));
    for (uint32_t i = 0; i < itemNum; i++) {
        *itemList[i].result = static_cast<int32_t>(ToCResult(static_cast<BResult>(*itemList[i].result)));
    }
    MMS_TRACE_END(SDK_TRACE_GET, ret);
    return ret;
}

CResult Mms::GetValuesByPrefix(const char *prefix, ValueInfo **valueInfoItems, uint64_t *itemNum)
{
    if (UNLIKELY(!KeyValid(prefix) || valueInfoItems == nullptr || itemNum == nullptr)) {
        return RET_MMS_EPERM;
    }

    MMS_TRACE_START(SDK_TRACE_PREFIX_SEARCH);
    CResult ret = ToCResult(gClient->GetValuesByPrefix(prefix, valueInfoItems, itemNum));
    MMS_TRACE_END(SDK_TRACE_PREFIX_SEARCH, ret);
    return ret;
}

CResult Mms::GetValuesByRange(const char *start, const char *end, ValueInfo **valueInfoItems, uint64_t *itemNum)
{
    if (UNLIKELY(!RangeValid(start, end) || valueInfoItems == nullptr || itemNum == nullptr)) {
        return RET_MMS_EPERM;
    }

    MMS_TRACE_START(SDK_TRACE_RANGE_SEARCH);
    CResult ret = ToCResult(gClient->GetValuesByRange(start, end, valueInfoItems, itemNum));
    MMS_TRACE_END(SDK_TRACE_RANGE_SEARCH, ret);
    return ret;
}

CResult Mms::BatchDeleteByRange(const char *start, const char *end)
{
    if (UNLIKELY(!RangeValid(start, end))) {
        return RET_MMS_EPERM;
    }

    MMS_TRACE_START(SDK_TRACE_RANGE_DELETE);
    CResult ret = ToCResult(gClient->BatchDeleteByRange(start, end));
    MMS_TRACE_END(SDK_TRACE_RANGE_DELETE, ret);
    return ret;
}

void Mms::FreeResources(ValueInfo **valueInfoItems, uint64_t itemNum)
{
    if (valueInfoItems == nullptr || *valueInfoItems == nullptr || itemNum == 0) {
        return;
    }

    gClient->FreeResources(valueInfoItems, itemNum);
}

CResult Mms::Update(UpdateItems *itemList, uint32_t itemNum)
{
    MMS_TRACE_START(SDK_TRACE_UPDATE);
    if (UNLIKELY(itemList == nullptr || itemNum == 0)) {
        return RET_MMS_EPERM;
    }

    for (uint32_t i = 0; i < itemNum; i++) {
        if (UNLIKELY(!KeyValid(itemList[i].key, itemList[i].keyLen) || itemList[i].value == nullptr ||
                     itemList[i].valueLen == 0 || itemList[i].result == nullptr)) {
            return RET_MMS_EPERM;
        }
        *itemList[i].result = static_cast<int32_t>(MMS_OK);
    }

    auto ret = ToCResult(gClient->MmsUpdate(itemList, itemNum));
    for (uint32_t i = 0; i < itemNum; i++) {
        *itemList[i].result = static_cast<int32_t>(ToCResult(static_cast<BResult>(*itemList[i].result)));
    }
    MMS_TRACE_END(SDK_TRACE_UPDATE, ret);
    return ret;
}

CResult Mms::Delete(DeleteItems *itemList, uint32_t itemNum)
{
    MMS_TRACE_START(SDK_TRACE_DELETE);
    if (UNLIKELY(itemList == nullptr || itemNum == 0)) {
        return RET_MMS_EPERM;
    }
    for (uint32_t i = 0; i < itemNum; i++) {
        if (!KeyValid(itemList[i].key, itemList[i].keyLen) || itemList[i].result == nullptr) {
            return RET_MMS_EPERM;
        }
        *itemList[i].result = static_cast<int32_t>(MMS_OK);
    }
    auto ret = ToCResult(gClient->MmsDelete(itemList, itemNum));
    for (uint32_t i = 0; i < itemNum; i++) {
        *itemList[i].result = static_cast<int32_t>(ToCResult(static_cast<BResult>(*itemList[i].result)));
    }
    MMS_TRACE_END(SDK_TRACE_DELETE, ret);
    return ret;
}

CResult Mms::Replace(ReplaceItems *itemList, uint32_t itemNum)
{
    MMS_TRACE_START(SDK_TRACE_REPLACE);
    if (UNLIKELY(itemList == nullptr || itemNum == 0)) {
        return RET_MMS_EPERM;
    }

    for (uint32_t i = 0; i < itemNum; i++) {
        if (UNLIKELY(!KeyValid(itemList[i].key, itemList[i].keyLen) || itemList[i].value == nullptr ||
                     itemList[i].valueLen == 0 || itemList[i].result == nullptr)) {
            return RET_MMS_EPERM;
        }
        *itemList[i].result = static_cast<int32_t>(MMS_OK);
    }

    auto ret = ToCResult(gClient->MmsReplace(itemList, itemNum));
    for (uint32_t i = 0; i < itemNum; i++) {
        *itemList[i].result = static_cast<int32_t>(ToCResult(static_cast<BResult>(*itemList[i].result)));
    }
    MMS_TRACE_END(SDK_TRACE_REPLACE, ret);
    return ret;
}

CResult Mms::StartCatchUpTask()
{
    MMS_TRACE_START(SDK_TRACE_CATCH_UP);
    auto ret = ToCResult(gClient->MmsStartCatchUpTask());
    MMS_TRACE_END(SDK_TRACE_CATCH_UP, ret)
    return ret;
}
}
}

CResult MmsInitialize(const MmsOptions *options, ServiceCallback service)
{
    if (options == nullptr) {
        return RET_MMS_EPERM;
    }
    return ock::mms::Mms::Initialize(*options, service);
}

CResult MmsRegisterNotifyCallback(NotifyCallback callback)
{
    return ock::mms::Mms::RegisterCallback(callback);
}

void MmsExit()
{
    ock::mms::Mms::Exit();
}

CResult MmsPut(PutItems *itemList, uint32_t itemNum)
{
    return ock::mms::Mms::Put(itemList, itemNum);
}

CResult MmsGet(GetItems *itemList, uint32_t itemNum)
{
    return ock::mms::Mms::Get(itemList, itemNum);
}

CResult MmsGetValuesByPrefix(const char *prefix, ValueInfo **valueInfoItems, uint64_t *itemNum)
{
    return ock::mms::Mms::GetValuesByPrefix(prefix, valueInfoItems, itemNum);
}

CResult MmsGetValuesByRange(const char *start, const char *end, ValueInfo **valueInfoItems, uint64_t *itemNum)
{
    return ock::mms::Mms::GetValuesByRange(start, end, valueInfoItems, itemNum);
}

void MmsFreeResources(ValueInfo **valueInfoItems, uint64_t itemNum)
{
    return ock::mms::Mms::FreeResources(valueInfoItems, itemNum);
}

CResult MmsUpdate(UpdateItems *itemList, uint32_t itemNum)
{
    return ock::mms::Mms::Update(itemList, itemNum);
}

CResult MmsDelete(DeleteItems *itemList, uint32_t itemNum)
{
    return ock::mms::Mms::Delete(itemList, itemNum);
}

CResult MmsBatchDeleteByRange(const char *start, const char *end)
{
    return ock::mms::Mms::BatchDeleteByRange(start, end);
}

CResult MmsReplace(ReplaceItems *itemList, uint32_t itemNum)
{
    return ock::mms::Mms::Replace(itemList, itemNum);
}

CResult MmsStartCatchUpTask()
{
    return ock::mms::Mms::StartCatchUpTask();
}
