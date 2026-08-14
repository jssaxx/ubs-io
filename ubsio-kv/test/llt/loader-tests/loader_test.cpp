/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 */

#include <gtest/gtest.h>

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
}

}  // namespace
}  // namespace ubsio
}  // namespace ock
