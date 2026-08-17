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

inline static bool ValueValid(const char *value, uint32_t valueLen)
{
    return value != nullptr && valueLen != 0 && valueLen <= MAX_VALUE_SIZE;
}

inline static bool UpdateValueValid(const char *value, uint32_t valueLen, uint32_t offset)
{
    return ValueValid(value, valueLen) && offset <= MAX_VALUE_SIZE - valueLen;
}

inline static bool IoRangeValid(uint32_t offset, uint32_t length)
{
    return length != 0 && length <= MAX_VALUE_SIZE && offset <= MAX_VALUE_SIZE - length;
}

inline static bool GetItemValid(const GetItems &item)
{
    if (UNLIKELY(!KeyValid(item.key, item.keyLen) || item.value == nullptr || item.realLength == nullptr ||
                 item.result == nullptr)) {
        return false;
    }
    return *item.value == nullptr || IoRangeValid(item.offset, item.length);
}

CResult Mms::Initialize(const MmsOptions &options, ServiceCallback service)
{
    if (UNLIKELY(gClient == nullptr)) {
        CLIENT_LOG_ERROR("Get client instance failed.");
        return RET_MMS_ERROR;
    }
    return ToCResult(gClient->Initialize(options, service));
}

CResult Mms::RegisterCallback(NotifyCallback callback, void *lpUserData)
{
    if (UNLIKELY(gClient == nullptr)) {
        CLIENT_LOG_ERROR("Get client instance failed.");
        return RET_MMS_ERROR;
    }
    return ToCResult(gClient->RegisterNotifyCallback(callback, lpUserData));
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
        if (UNLIKELY(!KeyValid(itemList[i].key, itemList[i].keyLen) ||
                     !ValueValid(itemList[i].value, itemList[i].valueLen) || itemList[i].valueAddr == nullptr ||
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
        if (UNLIKELY(!GetItemValid(itemList[i]))) {
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

CResult Mms::Update(UpdateItems *itemList, uint32_t itemNum)
{
    MMS_TRACE_START(SDK_TRACE_UPDATE);
    if (UNLIKELY(itemList == nullptr || itemNum == 0)) {
        return RET_MMS_EPERM;
    }

    for (uint32_t i = 0; i < itemNum; i++) {
        if (UNLIKELY(!KeyValid(itemList[i].key, itemList[i].keyLen) ||
                     !UpdateValueValid(itemList[i].value, itemList[i].valueLen, itemList[i].offset) ||
                     itemList[i].result == nullptr)) {
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
        if (UNLIKELY(!KeyValid(itemList[i].key, itemList[i].keyLen) ||
                     !UpdateValueValid(itemList[i].value, itemList[i].valueLen, itemList[i].offset) ||
                     itemList[i].result == nullptr)) {
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

CResult MmsRegisterNotifyCallback(NotifyCallback callback, void *lpUserData)
{
    return ock::mms::Mms::RegisterCallback(callback, lpUserData);
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

CResult MmsUpdate(UpdateItems *itemList, uint32_t itemNum)
{
    return ock::mms::Mms::Update(itemList, itemNum);
}

CResult MmsDelete(DeleteItems *itemList, uint32_t itemNum)
{
    return ock::mms::Mms::Delete(itemList, itemNum);
}

CResult MmsReplace(ReplaceItems *itemList, uint32_t itemNum)
{
    return ock::mms::Mms::Replace(itemList, itemNum);
}

CResult MmsStartCatchUpTask()
{
    return ock::mms::Mms::StartCatchUpTask();
}
