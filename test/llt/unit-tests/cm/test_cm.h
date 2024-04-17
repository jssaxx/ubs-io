/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */
#ifndef BOOSTIO_TEST_CM_H
#define BOOSTIO_TEST_CM_H

#include "gtest/gtest.h"

namespace ock {
namespace bio {
class TestCm : public testing::Test {
    void SetUp() override;

    void TearDown() override;

public:
    static void Stub();
    static void CancelNodeStub();

private:
    static bool gSetup;
};
}
}
#endif // BOOSTIO_TEST_CM_H