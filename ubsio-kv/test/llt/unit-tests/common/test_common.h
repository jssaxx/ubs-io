/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 */

#ifndef UBSIO_KV_UT_TEST_COMMON_H
#define UBSIO_KV_UT_TEST_COMMON_H

#include <cstdlib>
#include <string>
#include <gtest/gtest.h>
#include "dl_acl_api.h"
#include "dl_biosdk_api.h"
#include "stub_control.h"
#include "ubsio_kvc_operation.h"
#include "ubsio_nds_dl_api.h"
#include "ubsio_nds_manager.h"

class KvTest : public testing::Test {
protected:
    void SetUp() override
    {
        ResetRuntime();
        FakeBioReset();
        FakeAclReset();
        FakeNdsReset();
        unsetenv("UBSIO_BIO_DISKS");
        unsetenv("UBSIO_USE_IO_URING");
        unsetenv("UBSIO_NDS_READ_THREAD");
    }

    void TearDown() override
    {
        ResetRuntime();
        unsetenv("UBSIO_BIO_DISKS");
        unsetenv("UBSIO_USE_IO_URING");
        unsetenv("UBSIO_NDS_READ_THREAD");
    }

    static void ResetRuntime()
    {
        ock::ubsio::KvcExit();
        ock::ubsio::nds::NdsManager::Instance().UnInitialize();
        ock::ubsio::ACLApi::CleanupLibrary();
        ock::ubsio::NdsApi::CleanupLibrary();
        ock::ubsio::DlBioSdkApi::CleanupLibrary();
    }

    static std::string AscendHome()
    {
        const char *value = std::getenv("ASCEND_HOME_PATH");
        return value == nullptr ? "" : value;
    }
};

#endif
