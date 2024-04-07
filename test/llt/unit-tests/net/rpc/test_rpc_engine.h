/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_TEST_RPC_ENGINE_H
#define BOOSTIO_TEST_RPC_ENGINE_H

#include "gtest/gtest.h"

namespace ock {
namespace bio {
class TestRpcEngine : public testing::Test {
    void SetUp() override;

    void TearDown() override;

public:
    static void Stub();

private:
    static bool gSetup;
};
}
}
#endif // BOOSTIO_TEST_RPC_ENGINE_H
