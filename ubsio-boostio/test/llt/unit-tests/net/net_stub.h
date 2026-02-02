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

#ifndef NET_STUB_H
#define NET_STUB_H

#include <cstdint>
#include <cstdlib>
#include <string>
#include <atomic>
#include "net_common.h"

namespace ock {
namespace bio {
class NetStub {
public:
    static int32_t Connect(const std::string &serverUrl, ock::hcom::UBSHcomChannelPtr &ch,
                           const ock::hcom::UBSHcomConnectOptions &opt = {});

    static int32_t Call(const ock::hcom::UBSHcomRequest &req, ock::hcom::UBSHcomResponse &rsp);

    static int32_t AsyncCall(const ock::hcom::UBSHcomRequest &req, ock::hcom::UBSHcomResponse &rsp, Callback callback);

    static int32_t Get(const NetRequest &request);

    static int32_t Put(const NetRequest &request);

    static int32_t SendFds(int fds[], uint32_t len);

    static int32_t ReceiveFds(int fds[], uint32_t len, int32_t ts);

    static int32_t Reply(int32_t retCode, void *resp, uint32_t respSize);

    static std::atomic<uint64_t> connectCount;
};
}
}

#endif