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
#include "mms_message.h"
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

TEST_F(TestCommon, test_decode_request_layout_validation)
{
    UpdateCrcSwitch(false);
    constexpr uint32_t keyLen = NO_3;
    constexpr uint32_t encodedKeyLen = keyLen + NO_1;
    constexpr uint32_t valueLen = NO_3;
    uint64_t requestLen = sizeof(IoDataRequest) + sizeof(IoLocDesc) + encodedKeyLen + valueLen;
    std::vector<uint8_t> request(requestLen, 0);
    auto *req = reinterpret_cast<IoDataRequest *>(request.data());
    req->num = NO_1;
    auto *desc = reinterpret_cast<IoLocDesc *>(request.data() + sizeof(IoDataRequest));
    desc->keyLen = encodedKeyLen;
    desc->valueLen = valueLen;
    auto *key = reinterpret_cast<char *>(desc + NO_1);
    ASSERT_EQ(memcpy_s(key, encodedKeyLen, "key", keyLen), EOK);
    key[keyLen] = '\0';
    auto *value = key + encodedKeyLen;
    ASSERT_EQ(memcpy_s(value, valueLen, "val", valueLen), EOK);

    uint32_t itemNum = 0;
    std::vector<DecodePutItem> putItems(NO_1);
    EXPECT_EQ(DeCodePutRequest(putItems, itemNum, reinterpret_cast<uint64_t>(request.data()), request.size()), MMS_OK);
    ASSERT_EQ(itemNum, NO_1);
    EXPECT_EQ(putItems[NO_0].keyLen, keyLen);
    EXPECT_EQ(putItems[NO_0].valueLen, valueLen);

    std::vector<DecodeUpdateItem> updateItems(NO_1);
    EXPECT_EQ(DeCodeUpdateRequest(updateItems, itemNum, reinterpret_cast<uint64_t>(request.data()), request.size()),
              MMS_OK);
    EXPECT_EQ(DeCodeReplaceRequest(updateItems, itemNum, reinterpret_cast<uint64_t>(request.data()), request.size()),
              MMS_OK);

    EXPECT_EQ(DeCodePutRequest(putItems, itemNum, reinterpret_cast<uint64_t>(request.data()), request.size() - NO_1),
              MMS_INVALID_PARAM);
    desc->keyLen = 0;
    EXPECT_EQ(DeCodePutRequest(putItems, itemNum, reinterpret_cast<uint64_t>(request.data()), request.size()),
              MMS_INVALID_PARAM);
    desc->keyLen = encodedKeyLen;
    key[keyLen] = 'x';
    EXPECT_EQ(DeCodePutRequest(putItems, itemNum, reinterpret_cast<uint64_t>(request.data()), request.size()),
              MMS_INVALID_PARAM);
    key[keyLen] = '\0';
    EXPECT_EQ(DeCodePutRequest(putItems, itemNum, 0, 0), MMS_INVALID_PARAM);

    uint64_t deleteLen = sizeof(IoDataRequest) + sizeof(IoLocDesc) + encodedKeyLen;
    std::vector<uint8_t> deleteRequest(deleteLen, 0);
    auto *deleteReq = reinterpret_cast<IoDataRequest *>(deleteRequest.data());
    deleteReq->num = NO_1;
    auto *deleteDesc = reinterpret_cast<IoLocDesc *>(deleteRequest.data() + sizeof(IoDataRequest));
    deleteDesc->keyLen = encodedKeyLen;
    auto *deleteKey = reinterpret_cast<char *>(deleteDesc + NO_1);
    ASSERT_EQ(memcpy_s(deleteKey, encodedKeyLen, "key", keyLen), EOK);
    deleteKey[keyLen] = '\0';
    std::vector<DecodeDeleteItem> deleteItems(NO_1);
    EXPECT_EQ(DeCodeDeleteRequest(deleteItems, itemNum, reinterpret_cast<uint64_t>(deleteRequest.data()),
                                  deleteRequest.size()), MMS_OK);
    deleteDesc->valueLen = NO_1;
    EXPECT_EQ(DeCodeDeleteRequest(deleteItems, itemNum, reinterpret_cast<uint64_t>(deleteRequest.data()),
                                  deleteRequest.size()), MMS_INVALID_PARAM);
}
