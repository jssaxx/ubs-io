/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "dfc_log.h"
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
        LOG_ERROR("Create stream failed");
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