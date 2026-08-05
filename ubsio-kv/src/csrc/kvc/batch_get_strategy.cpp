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

#include "batch_get_strategy.h"
#include "dl_biosdk_api.h"
#include "ubsio_kvc_err.h"
#include "ubsio_kvc_log.h"
#include <cstring>
#include <vector>

namespace ock {
namespace ubsio {

int32_t BatchGetStandalone::Execute(const BatchGetContext &ctx)
{
    // STANDALONE: zero-copy, pass user buffers directly to SDK
    return DlBioSdkApi::BatchGet(ctx.tenantId, ctx.keys, ctx.count, ctx.offsets, ctx.lengths,
        ctx.locations, reinterpret_cast<uintptr_t *>(ctx.userBufs), ctx.realLengths, ctx.results);
}

int32_t BatchGetSeparates::Execute(const BatchGetContext &ctx)
{
    // SEPARATES: SDK returns its own registered memory, need copy + free
    std::vector<uintptr_t> sdkBuffers(ctx.count, 0);
    CResult ret = DlBioSdkApi::BatchGet(ctx.tenantId, ctx.keys, ctx.count, ctx.offsets, ctx.lengths,
        ctx.locations, sdkBuffers.data(), ctx.realLengths, ctx.results);
    if (ret != CResult::RET_CACHE_OK) {
        return ret;
    }

    for (uint32_t i = 0; i < ctx.count; ++i) {
        if (ctx.results[i] != UBSIO_KVC_OK) {
            continue;
        }
        if (sdkBuffers[i] == 0 || ctx.realLengths[i] == 0 || ctx.realLengths[i] > ctx.lengths[i]) {
            LOG_ERROR("Invalid batch get buffer, index:" << i
                << ", address:" << sdkBuffers[i]
                << ", realLength:" << ctx.realLengths[i]
                << ", bufferLength:" << ctx.lengths[i]);
            ctx.results[i] = UBSIO_KVC_ERR;
            continue;
        }
        std::memcpy(ctx.userBufs[i], reinterpret_cast<void *>(sdkBuffers[i]), ctx.realLengths[i]);
    }

    CResult freeRet = DlBioSdkApi::BatchGetFree(ctx.tenantId, sdkBuffers.data(), ctx.count);
    if (freeRet != CResult::RET_CACHE_OK) {
        LOG_ERROR("Free batch get sdk buffers failed, ret:" << freeRet);
        return UBSIO_KVC_ERR;
    }
    return ret;
}

} // namespace ubsio
} // namespace ock
