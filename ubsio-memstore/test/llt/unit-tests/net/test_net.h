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

#ifndef MMS_TEST_NET_H
#define MMS_TEST_NET_H

#include "gtest/gtest.h"

namespace ock {
namespace mms {
class TestNet : public testing::Test {
    void SetUp() override;

    void TearDown() override;

private:
    static bool gSetup;
};
}  // namespace mms
}  // namespace ock

#endif  // MMS_TEST_NET_H

