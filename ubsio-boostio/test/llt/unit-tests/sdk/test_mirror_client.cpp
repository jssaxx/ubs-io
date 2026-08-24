/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "bio_client_log.h"
#include "bio_execution.h"
#include "bio_monotonic.h"
#include "bio_trace.h"
#include "bio_tracepoint_helper.h"

#define private public
#include "mirror_client.h"
#undef private

using namespace ock::bio;

TEST(TestMirrorClient, standalone_delete_checks_identity_and_erases_matching_flow_without_delay)
{
    constexpr uint16_t ptId = 3;
    constexpr uint64_t flowId = 99;
    MirrorClient client(STANDALONE);
    ASSERT_EQ(client.Insert(ptId), BIO_OK);
    MirrorClient::UpdateParams params{ ptId, 2, flowId, false, 0, 0 };
    ASSERT_EQ(client.Update(params), BIO_OK);

    client.Delete(ptId + 1, flowId);
    EXPECT_EQ(client.mFlowMap.size(), 1);
    client.Delete(ptId, flowId + 1);
    EXPECT_EQ(client.mFlowMap.size(), 1);

    CmPtInfo ptInfo;
    ptInfo.ptId = ptId;
    ptInfo.version = 2;
    ptInfo.state = CM_PT_NORMAL;
    client.mPtView.emplace(ptId, ptInfo);
    client.Delete(ptId, flowId);
    EXPECT_TRUE(client.mFlowMap.empty());
}
