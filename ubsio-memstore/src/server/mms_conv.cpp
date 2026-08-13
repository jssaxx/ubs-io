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
#include "mms_notify.h"
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

inline static bool KeyValid(const char *key, uint16_t keyLen)
{
    if (UNLIKELY(key == nullptr || keyLen == 0 || keyLen >= MAX_KEY_SIZE)) {
        return false;
    }
    return true;
}

inline static bool GetItemValid(const GetItems &item)
{
    if (UNLIKELY(!KeyValid(item.key, item.keyLen) || item.value == nullptr || item.realLength == nullptr ||
                 item.result == nullptr)) {
        return false;
    }
    return *item.value == nullptr || item.length != 0;
}

CResult MmsConv::Initialize(const MmsOptions &options, ServiceCallback service)
{
    auto mmsServer = MmsServer::Instance();
    auto ret = mmsServer->Start(service);
    return ToCResult(ret);
}

CResult MmsConv::RegisterCallback(NotifyCallback callback, void *lpUserData)
{
    if (isSeparateMode) {
        return RET_MMS_OK;
    }

    if (callback != nullptr && !MmsServer::Instance()->GetConfig()->GetBasicConfig().dataChangeCallbackSwitch) {
        return RET_MMS_OK;
    }

    return MmsNotifyDispatcher::Instance().RegisterCallback(callback, lpUserData);
}

void MmsConv::Exit()
{
    MmsNotifyDispatcher::Instance().Stop();
    auto mmsServer = MmsServer::Instance();
    mmsServer->Exit();
}

CResult MmsConv::Put(PutItems *itemList, uint32_t itemNum)
{
    MMS_TRACE_START(SDK_TRACE_PUT);
    if (isSeparateMode) {
        return RET_MMS_PROTECTED;
    }

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

    auto ret = ToCResult(gServer->Put(itemList, itemNum));
    for (uint32_t i = 0; i < itemNum; i++) {
        *itemList[i].result = static_cast<int32_t>(ToCResult(static_cast<BResult>(*itemList[i].result)));
    }
    MMS_TRACE_END(SDK_TRACE_PUT, ret);
    return ret;
}

CResult MmsConv::Get(GetItems *itemList, uint32_t itemNum)
{
    MMS_TRACE_START(SDK_TRACE_GET);
    if (isSeparateMode) {
        return RET_MMS_PROTECTED;
    }

    if (UNLIKELY(itemList == nullptr || itemNum == 0)) {
        return RET_MMS_EPERM;
    }

    for (uint32_t i = 0; i < itemNum; i++) {
        if (UNLIKELY(!GetItemValid(itemList[i]))) {
            return RET_MMS_EPERM;
        }
        *itemList[i].result = static_cast<int32_t>(MMS_OK);
    }

    auto ret = ToCResult(gServer->Get(itemList, itemNum));
    for (uint32_t i = 0; i < itemNum; i++) {
        *itemList[i].result = static_cast<int32_t>(ToCResult(static_cast<BResult>(*itemList[i].result)));
    }
    MMS_TRACE_END(SDK_TRACE_GET, ret);
    return ret;
}

CResult MmsConv::Update(UpdateItems *itemList, uint32_t itemNum)
{
    MMS_TRACE_START(SDK_TRACE_UPDATE);
    if (isSeparateMode) {
        return RET_MMS_PROTECTED;
    }
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

    auto ret = ToCResult(gServer->Update(itemList, itemNum));
    for (uint32_t i = 0; i < itemNum; i++) {
        *itemList[i].result = static_cast<int32_t>(ToCResult(static_cast<BResult>(*itemList[i].result)));
    }
    MMS_TRACE_END(SDK_TRACE_UPDATE, ret);
    return ret;
}

CResult MmsConv::Delete(DeleteItems *itemList, uint32_t itemNum)
{
    MMS_TRACE_START(SDK_TRACE_DELETE);
    if (isSeparateMode) {
        return RET_MMS_PROTECTED;
    }
    if (UNLIKELY(itemList == nullptr || itemNum == 0)) {
        return RET_MMS_EPERM;
    }
    for (uint32_t i = 0; i < itemNum; i++) {
        if (!KeyValid(itemList[i].key, itemList[i].keyLen) || itemList[i].result == nullptr) {
            return RET_MMS_EPERM;
        }
        *itemList[i].result = static_cast<int32_t>(MMS_OK);
    }
    auto ret = ToCResult(gServer->Delete(itemList, itemNum));
    for (uint32_t i = 0; i < itemNum; i++) {
        *itemList[i].result = static_cast<int32_t>(ToCResult(static_cast<BResult>(*itemList[i].result)));
    }
    MMS_TRACE_END(SDK_TRACE_DELETE, ret);
    return ret;
}

CResult MmsConv::Replace(ReplaceItems *itemList, uint32_t itemNum)
{
    MMS_TRACE_START(SDK_TRACE_REPLACE);
    if (isSeparateMode) {
        return RET_MMS_PROTECTED;
    }

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

    auto ret = ToCResult(gServer->Replace(itemList, itemNum));
    for (uint32_t i = 0; i < itemNum; i++) {
        *itemList[i].result = static_cast<int32_t>(ToCResult(static_cast<BResult>(*itemList[i].result)));
    }
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

CResult MmsInitialize(const MmsOptions *options, ServiceCallback service)
{
    if (options == nullptr) {
        return RET_MMS_EPERM;
    }
    return ock::mms::MmsConv::Initialize(*options, service);
}

CResult MmsRegisterNotifyCallback(NotifyCallback callback, void *lpUserData)
{
    return ock::mms::MmsConv::RegisterCallback(callback, lpUserData);
}

void MmsExit()
{
    ock::mms::MmsConv::Exit();
}

CResult MmsPut(PutItems *itemList, uint32_t itemNum)
{
    return ock::mms::MmsConv::Put(itemList, itemNum);
}

CResult MmsGet(GetItems *itemList, uint32_t itemNum)
{
    return ock::mms::MmsConv::Get(itemList, itemNum);
}

CResult MmsUpdate(UpdateItems *itemList, uint32_t itemNum)
{
    return ock::mms::MmsConv::Update(itemList, itemNum);
}

CResult MmsDelete(DeleteItems *itemList, uint32_t itemNum)
{
    return ock::mms::MmsConv::Delete(itemList, itemNum);
}

CResult MmsReplace(ReplaceItems *itemList, uint32_t itemNum)
{
    return ock::mms::MmsConv::Replace(itemList, itemNum);
}

CResult MmsStartCatchUpTask()
{
    return ock::mms::MmsConv::StartCatchUpTask();
}
