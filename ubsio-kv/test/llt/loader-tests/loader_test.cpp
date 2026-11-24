/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>
#include <dlfcn.h>

#include "dl_acl_api.h"
#include "dl_biosdk_api.h"
#include "ubsio_kvc_err.h"
#include "ubsio_nds_dl_api.h"

namespace ock {
namespace ubsio {
namespace {

TEST(LoaderFailureTest, MissingRequiredSymbolsReturnError)
{
    EXPECT_EQ(DlBioSdkApi::LoadLibrary(), UBSIO_KVC_ERR);
    EXPECT_EQ(ACLApi::LoadLibrary(), UBSIO_KVC_ERR);
    EXPECT_EQ(NdsApi::LoadLibrary(), UBSIO_KVC_ERR);
    EXPECT_EQ(NdsApi::DriverOpen(), -ENOSYS);
}

TEST(LoaderFailureTest, MissingConfigInfoSymbolRejectsLibrary)
{
    void *handle = dlopen("libbio_sdk.so", RTLD_NOW | RTLD_LOCAL);
    ASSERT_NE(handle, nullptr);
    EXPECT_NE(dlsym(handle, "BioInitialize"), nullptr);
    EXPECT_NE(dlsym(handle, "BioFreeScanKeyResult"), nullptr);
    EXPECT_EQ(dlsym(handle, "BioGetConfigInfo"), nullptr);
    EXPECT_EQ(dlclose(handle), 0);

    EXPECT_EQ(DlBioSdkApi::LoadLibrary(), UBSIO_KVC_ERR);
    BioConfigInfo info{};
    EXPECT_EQ(DlBioSdkApi::GetConfigInfo(&info), RET_CACHE_NOT_READY);
}

}  // namespace
}  // namespace ubsio
}  // namespace ock
