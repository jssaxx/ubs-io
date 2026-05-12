/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "test_common.h"
#include "mms_log.h"
#include "mms_comm.h"
#include "mms_crc_util.h"
#include "mms_sequence.h"

using namespace ock::mms;

bool TestCommon::gSetup = false;

void TestCommon::SetUp()
{
    if (gSetup) {
        return;
    }
    gSetup = true;
    return;
}

void TestCommon::TearDown()
{
    return;
}

TEST_F(TestCommon, test_setgroupInfo_return_ok)
{
    LOG_INFO("test_setgroupInfo_return_ok");
    NumaGroupIndexPtr numaGroupIndexPtr = NumaGroupIndex::Instance();
    numaGroupIndexPtr->SetGroupInfo(8);
    EXPECT_EQ(numaGroupIndexPtr->GetGroupNum(), 8);
}

TEST_F(TestCommon, test_crc_soft_32)
{
    LOG_INFO("test_crc_soft_32");
    uint64_t length = 128;
    char *data = reinterpret_cast<char *>(malloc(length));
    memset_s(data, length, 'F', length);
    auto ret = MmsCrcUtil::SoftCrc32(data, length);
    auto hardRet = MmsCrcUtil::HardCrc32(data, length);
    auto crc = MmsCrcUtil::Crc32(data, length);
    EXPECT_NE(ret, 0U);
    EXPECT_NE(hardRet, 0U);
    EXPECT_TRUE(crc == ret || crc == hardRet);
    free(data);
}

TEST_F(TestCommon, test_sequence)
{
    LOG_INFO("test_sequence");
    MmsSequencePtr mmsSequencePtr = MmsSequence::Instance();
    auto ret = mmsSequencePtr->Initialize(NO_0, NO_0);
    EXPECT_EQ(ret, MMS_ERR);
    uint64_t lev1Cap = 16;
    uint64_t lev2Cap = 16;
    ret = mmsSequencePtr->Initialize(lev1Cap, lev2Cap);
    EXPECT_EQ(ret, MMS_OK);
    uint64_t lev1Id = 1;
    uint64_t lev2Id = 1;
    uint64_t seqNo = 0;
    uint64_t negoSeqNo = 0;
    mmsSequencePtr->SetEnable(true);
    ret = mmsSequencePtr->ApplyForSeqNo2Mst(lev1Id, lev2Id, seqNo, negoSeqNo);
    EXPECT_EQ(ret, MMS_OK);
    uint32_t length = 128;
    char *data = reinterpret_cast<char *>(malloc(length));
    memset_s(data, length, 'F', length);
    ret = mmsSequencePtr->NegoSeqNo2Slv(lev1Id, lev2Id, seqNo, data, length, negoSeqNo);
    EXPECT_EQ(ret, MMS_OK);
    uint64_t seqNoList[lev1Cap];
    uint32_t seqNum = 0;
    ret = mmsSequencePtr->GetSeqNoList2Slv(lev1Id, lev2Id, seqNoList, seqNum);
    EXPECT_EQ(ret, MMS_OK);
    char *data1 = reinterpret_cast<char *>(malloc(length));
    ret = mmsSequencePtr->GetSeqNoData2Slv(lev1Id, lev2Id, seqNo, data1, length);
    EXPECT_EQ(ret, MMS_OK);
    ret = memcmp(data, data1, length);
    EXPECT_EQ(ret, 0);
    ret = mmsSequencePtr->ReleaseSeqNo2Mst(lev1Id, lev2Id, seqNo);
    EXPECT_EQ(ret, MMS_OK);
    ret = mmsSequencePtr->ResetSeqNoState2Mst(lev1Id, lev2Id, seqNo);
    EXPECT_EQ(ret, MMS_OK);
    free(data1);
    free(data);
}
