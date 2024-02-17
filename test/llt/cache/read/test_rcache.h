//
// Created by root on 12/11/23.
//

#ifndef BOOSTIO_TEST_RCACHE_H
#define BOOSTIO_TEST_RCACHE_H

#include "gtest/gtest.h"

class TestRCache : public testing::Test{
    void SetUp() override;

    void TearDown() override;
public:
    static void Stub();
private:
    static bool g_setup;
};

#endif // BOOSTIO_TEST_RCACHE_H