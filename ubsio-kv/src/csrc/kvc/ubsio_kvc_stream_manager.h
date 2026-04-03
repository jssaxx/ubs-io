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