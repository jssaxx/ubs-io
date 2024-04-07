/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_TEST_HTRACER_H
#define BOOSTIO_TEST_HTRACER_H

#include <cstdint>
#include "gtest/gtest.h"

namespace ock {
namespace bio {
class TestHtracer : public testing::Test {
    void SetUp() override;

    void TearDown() override;

public:
    static void Stub() noexcept;

    static void ServerMock() noexcept;

private:
    static int32_t HTracerInitMock(const std::string &dumpDir);

    static bool gSetup;
};
}
}

#endif // BOOSTIO_TEST_HTRACER_H
