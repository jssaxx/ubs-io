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
 
#ifndef UBSIO_KVC_STREAM_MANAGER_H
#define UBSIO_KVC_STREAM_MANAGER_H

#include <cstdint>
#include <mutex>

namespace ock {
namespace ubsio {

class KvcStreamManager {
public:
    static void* GetAclStream();
    static void DestroyAclStream();
    static int32_t InitAclStream(int32_t deviceId);

private:
    static void* stream_;
    static std::mutex mutex_;
};

} // namespace ubsio
} // namespace ock
#endif // UBSIO_KVC_STREAM_MANAGER_H