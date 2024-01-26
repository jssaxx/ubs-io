//
// Created by root on 12/11/23.
//

#ifndef BOOSTIO_TEST_CM_H
#define BOOSTIO_TEST_CM_H

#include "gtest/gtest.h"

class TestCm : public testing::Test{
    void SetUp() override;

    void TearDown() override;
public:
    static void Stub();
private:
    static bool g_setup;
};

#endif //BOOSTIO_TEST_CM_H