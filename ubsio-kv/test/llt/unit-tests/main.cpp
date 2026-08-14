/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>

#include "ubsio_kvc_log.h"

namespace {
void SilentLogger(int, const char *)
{}
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    ock::ubsio::UbsioLog::Instance().SetExternalLogFunction(SilentLogger, true);
    return RUN_ALL_TESTS();
}
