/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <vector>
#include "ubsio_kvc_err.h"
#include "ubsio_kvc_log.h"
#include "dl_biosdk_api.h"
#include "dl_acl_api.h"
#include "ubsio_kvc_stream_manager.h"
#include "kv_operation.h"
#include "ubsio_kvc_operation.h"

namespace ock {
namespace ubsio {

static uint64_t tenantId = 1;
static KvOperation *g_kvOperation = KvOperation::Instance();

int32_t KvcOperationInit(int32_t devId)
{
    if (devId >= 0) {
        if (ACLApi::LoadLibrary() != DFC_OK) {
            return DFC_ERR;
        }
        if (DfcStreamManager::InitAclStream(devId) != DFC_OK) {
            return DFC_ERR;
        }
    }
    
    // dlopen and init biosdk
    int ret = DlBioSdkApi::LoadLibrary();
    if (ret != DFC_OK) {
        LOG_ERROR("dlopen bio sdk failed");
        return DFC_ERR;
    }
    ret = DlBioSdkApi::DfcKvBioInit();
    if (ret != DFC_OK) {
        LOG_ERROR("Kv bio init failed");
        return DFC_ERR;
    }

    // init kv operation executor
    ret = g_kvOperation->InitKvExecutor();
    if (ret != DFC_OK) {
        LOG_ERROR("kv init executor failed");
        return DFC_ERR;
    }

    LOG_INFO("Init ubsio kv cache success, deviceId:" << devId);
    return DFC_OK;
}

int32_t KvcPutData(const std::string &key, void *value, size_t len, uint32_t flags)
{
    if (UNLIKELY(g_kvOperation == nullptr)) {
        LOG_ERROR("kv operation is nullptr");
        return DFC_ERR;
    }
    return g_kvOperation->KvPutData(key, value, len);
}

int32_t KvcBatchExistKey(const std::vector<std::string> &key, bool *results, uint32_t flags)
{
    if (UNLIKELY(g_kvOperation == nullptr)) {
        LOG_ERROR("kv operation is nullptr");
        return DFC_ERR;
    }
    return g_kvOperation->BatchKvExistKey(key, results);
}

int32_t KvcBatchGetData(const std::vector<std::string> &key,
                        void **bufs,
                        std::vector<size_t> &lengths,
                        std::vector<int> &results,
                        uint32_t flags)
{
    if (UNLIKELY(g_kvOperation == nullptr)) {
        LOG_ERROR("kv operation is nullptr");
        return DFC_ERR;
    }
    return g_kvOperation->BatchKvGetData(key, bufs, lengths, results);
}

int32_t KvcBatchFreeGetAddress(void **bufs, uint32_t keys_count)
{
    if (UNLIKELY(g_kvOperation == nullptr)) {
        LOG_ERROR("kv operation is nullptr");
        return DFC_ERR;
    }
    return DlBioSdkApi::BatchGetFree(tenantId, reinterpret_cast<uintptr_t*>(bufs), keys_count);
}

} // namespace ubsio
} // namespace ock