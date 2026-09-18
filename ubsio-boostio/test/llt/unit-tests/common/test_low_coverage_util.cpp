/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <array>
#include <cstring>
#include <dlfcn.h>
#include "gtest/gtest.h"
#include "bio_tls_util.h"
#include "bdm_common.h"
#if defined(__x86_64__) || defined(__i386__)
#include "cpu_vendor_checker.h"
#endif
#include "disk_statistic.h"

using namespace ock::bio;

TEST(TestLowCoverageUtil, DefaultTlsDecrypterCopiesInput)
{
    constexpr char cipher[] = "boostio-secret";
    std::array<char, sizeof(cipher)> plain{};
    size_t plainLength = 0;

    EXPECT_EQ(TlsUtil::DefaultDecrypter(cipher, sizeof(cipher), plain.data(), &plainLength), 0);
    EXPECT_EQ(plainLength, sizeof(cipher));
    EXPECT_EQ(memcmp(cipher, plain.data(), sizeof(cipher)), 0);
}

TEST(TestLowCoverageUtil, TlsLibraryLoadingHandlesMissingLibraryAndSymbol)
{
    void **handler = TlsUtil::GetTlsLibHandler();
    ASSERT_NE(handler, nullptr);
    *handler = nullptr;
    TlsUtil::CloseTlsLib();

    EXPECT_EQ(TlsUtil::LoadDecryptFunction("/library/that/does/not/exist/libdecrypt.so"), nullptr);
    EXPECT_EQ(*handler, nullptr);

    EXPECT_EQ(TlsUtil::LoadDecryptFunction("libc.so.6"), nullptr);
    *handler = nullptr;

    *handler = dlopen(nullptr, RTLD_LAZY);
    ASSERT_NE(*handler, nullptr);
    TlsUtil::CloseTlsLib();
    *handler = nullptr;
}

#if defined(__x86_64__) || defined(__i386__)
TEST(TestLowCoverageUtil, CpuVendorQueriesValidateArgumentsAndReturnAnId)
{
    std::array<char, VENDOR_NAME_MAX_LEN> vendor{};
    EXPECT_EQ(GetCpuVendorId(nullptr, vendor.size()), BDM_CODE_ERR);
    EXPECT_EQ(GetCpuVendorId(vendor.data(), 0), BDM_CODE_ERR);
    ASSERT_EQ(GetCpuVendorId(vendor.data(), vendor.size()), BDM_CODE_OK);
    EXPECT_NE(vendor[0], '\0');
    EXPECT_EQ(vendor.back(), '\0');
    EXPECT_EQ(CheckCpuVendor(), BDM_CODE_OK);
}
#endif

TEST(TestLowCoverageUtil, DiskStatisticCountsHits)
{
    auto &statistic = DiskStatistic::Instance();
    uint64_t before = statistic.GetHitCount();
    statistic.IncHitCount();
    statistic.IncHitCount();
    EXPECT_EQ(statistic.GetHitCount(), before + 2);
}
