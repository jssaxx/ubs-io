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

inline static bool KeyValid(const char *key)
{
    if (UNLIKELY(key == nullptr || strlen(key) == 0 || strlen(key) >= MAX_KEY_SIZE)) {
        return false;
    }
    return true;
}

CResult Mms::Initialize(const MmsOptions &options, ServiceCallback service)
{
    if (UNLIKELY(gClient == nullptr)) {
        CLIENT_LOG_ERROR("Get client instance failed.");
        return RET_MMS_ERROR;
    }
    return ToCResult(gClient->Initialize(options, service));
}

void Mms::Exit()
{
    gClient->Exit();
}

CResult Mms::Put(uint64_t userId, PutItems *itemList, uint32_t itemNum)
{
    MMS_TRACE_START(SDK_TRACE_PUT);
    if (UNLIKELY(itemList == nullptr || itemNum == 0)) {
        return RET_MMS_EPERM;
    }

    for (uint32_t i = 0; i < itemNum; i++) {
        if (UNLIKELY(!KeyValid(itemList[i].key) || itemList[i].value == nullptr || itemList[i].length == 0)) {
            return RET_MMS_EPERM;
        }
    }

    auto ret = ToCResult(gClient->MmsPut(userId, itemList, itemNum));
    MMS_TRACE_END(SDK_TRACE_PUT, ret);
    return ret;
}

CResult Mms::Get(uint64_t userId, GetItems *itemList, uint32_t itemNum)
{
    MMS_TRACE_START(SDK_TRACE_GET);
    if (UNLIKELY(itemList == nullptr || itemNum == 0)) {
        return RET_MMS_EPERM;
    }

    for (uint32_t i = 0; i < itemNum; i++) {
        if (UNLIKELY(!KeyValid(itemList[i].key) || itemList[i].length == 0 || itemList[i].value == nullptr ||
                     itemList[i].realLength == nullptr)) {
            return RET_MMS_EPERM;
        }
    }

    auto ret = ToCResult(gClient->MmsGet(userId, itemList, itemNum));
    MMS_TRACE_END(SDK_TRACE_GET, ret);
    return ret;
}

CResult Mms::Update(uint64_t userId, UpdateItems *itemList, uint32_t itemNum)
{
    MMS_TRACE_START(SDK_TRACE_UPDATE);
    if (UNLIKELY(itemList == nullptr || itemNum == 0)) {
        return RET_MMS_EPERM;
    }

    for (uint32_t i = 0; i < itemNum; i++) {
        if (UNLIKELY(!KeyValid(itemList[i].key) || itemList[i].value == nullptr || itemList[i].length == 0)) {
            return RET_MMS_EPERM;
        }
    }

    auto ret = ToCResult(gClient->MmsUpdate(userId, itemList, itemNum));
    MMS_TRACE_END(SDK_TRACE_UPDATE, ret);
    return ret;
}

CResult Mms::Delete(uint64_t userId, DeleteItems *itemList, uint32_t itemNum)
{
    MMS_TRACE_START(SDK_TRACE_DELETE);
    if (UNLIKELY(itemList == nullptr || itemNum == 0)) {
        return RET_MMS_EPERM;
    }
    for (uint32_t i = 0; i < itemNum; i++) {
        if (!KeyValid(itemList[i].key)) {
            return RET_MMS_EPERM;
        }
    }
    auto ret = ToCResult(gClient->MmsDelete(userId, itemList, itemNum));
    MMS_TRACE_END(SDK_TRACE_DELETE, ret);
    return ret;
}

CResult Mms::Replace(uint64_t userId, ReplaceItems *itemList, uint32_t itemNum)
{
    MMS_TRACE_START(SDK_TRACE_REPLACE);
    if (UNLIKELY(itemList == nullptr || itemNum == 0)) {
        return RET_MMS_EPERM;
    }

    for (uint32_t i = 0; i < itemNum; i++) {
        if (UNLIKELY(!KeyValid(itemList[i].key) || itemList[i].value == nullptr || itemList[i].length == 0)) {
            return RET_MMS_EPERM;
        }
    }

    auto ret = ToCResult(gClient->MmsReplace(userId, itemList, itemNum));
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

CResult MmsInitialize(MmsOptions &options, ServiceCallback service)
{
    return ock::mms::Mms::Initialize(options, service);
}

void MmsExit()
{
    ock::mms::Mms::Exit();
}

CResult MmsPut(uint64_t userId, PutItems *itemList, uint32_t itemNum)
{
    return ock::mms::Mms::Put(userId, itemList, itemNum);
}

CResult MmsGet(uint64_t userId, GetItems *itemList, uint32_t itemNum)
{
    return ock::mms::Mms::Get(userId, itemList, itemNum);
}

CResult MmsUpdate(uint64_t userId, UpdateItems *itemList, uint32_t itemNum)
{
    return ock::mms::Mms::Update(userId, itemList, itemNum);
}

CResult MmsDelete(uint64_t userId, DeleteItems *itemList, uint32_t itemNum)
{
    return ock::mms::Mms::Delete(userId, itemList, itemNum);
}

CResult MmsReplace(uint64_t userId, ReplaceItems *itemList, uint32_t itemNum)
{
    return ock::mms::Mms::Replace(userId, itemList, itemNum);
}

CResult MmsStartCatchUpTask()
{
    return ock::mms::Mms::StartCatchUpTask();
}
