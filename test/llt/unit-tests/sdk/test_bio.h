/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_TEST_BIOSERVER_H
#define BOOSTIO_TEST_BIOSERVER_H

#include "gtest/gtest.h"

class TestBio : public testing::Test {
    void SetUp() override;

    void TearDown() override;
private:
    static bool gSetup;
};

#endif // BOOSTIO_TEST_BIOSERVER_H
