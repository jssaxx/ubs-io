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

#ifndef MMSCORE_TEST_MMS_H
#define MMSCORE_TEST_MMS_H

#include <cstdint>
#include <mockcpp/mockcpp.hpp>
#include "gtest/gtest.h"

namespace ock {
namespace mms {
class TestMms : public testing::Test {
    void SetUp() override;

    void TearDown() override;

public:
    static void Stub() noexcept;

    static void ServerMock() noexcept;

private:
    static bool gSetup;
};
}
}

#endif // MMSCORE_TEST_MMS_H

