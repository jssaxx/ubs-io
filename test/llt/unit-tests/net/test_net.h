/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef TEST_NET_H
#define TEST_NET_H

#include "gtest/gtest.h"

namespace ock {
namespace bio {
class TestNet : public testing::Test {
    void SetUp() override;

    void TearDown() override;

public:
    int32_t TestMsgHandle(ServiceContext &ctx);

private:
    static bool gSetup;
};
}
}
#endif // TEST_NET_H
