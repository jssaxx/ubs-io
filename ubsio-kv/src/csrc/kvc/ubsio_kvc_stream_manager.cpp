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

#include "ubsio_kvc_log.h"
#include "dl_acl_api.h"
#include "ubsio_kvc_stream_manager.h"

namespace ock {
namespace ubsio {

void* KvcStreamManager::stream_ = nullptr;
std::mutex KvcStreamManager::mutex_;

int32_t KvcStreamManager::InitAclStream(int32_t deviceId)
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (stream_ != nullptr) {
        return DFC_OK;
    }
    auto ret = ACLApi::AclrtSetDevice(deviceId);
    if (ret != DFC_OK) {
        LOG_ERROR("Set device failed, device id:" << deviceId);
        return DFC_ERR;
    }
    
    uint32_t aclStreamFastLaunch = 1U;
    uint32_t aclStreamFastSync = 2U;
    ret = ACLApi::AclrtCreateStreamWithConfig(&stream_, 0, aclStreamFastLaunch | aclStreamFastSync);
    if (ret != 0) {
        LOG_ERROR("Create stream failed, ret:" << ret);
        return DFC_ERR;
    }
    LOG_INFO("Init acl stream success, device id:" << deviceId);
    return DFC_OK;
}

void KvcStreamManager::DestroyAclStream()
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (stream_ == nullptr) {
        return;
    }
    ACLApi::AclrtDestroyStream(stream_);
    stream_ = nullptr;
}

void* KvcStreamManager::GetAclStream()
{
    return stream_;
}

}
}