/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#include "bio_qos.h"

using namespace ock::bio;

void BioQuota::Recycle(uint64_t size, QuotaType type, bool &isDispatch)
{
    mConcur[type]--;
    if (mCompensationQuota[type] != 0) {
        if (mCompensationQuota[type] > size) {
            mCompensationQuota[type] -= size;
            isDispatch = false;
        } else if (mCompensationQuota[type] < size) {
            mCurrentQuota[type] += (size - mCompensationQuota[type]);
            mCompensationQuota[type] = 0;
        } else {
            mCompensationQuota[type] = 0;
            isDispatch = false;
        }
    } else {
        mCurrentQuota[type] += size;
    }
}

void BioQuota::Dispatch(QuotaType type)
{
    if (mIoQueue[type].Empty()) {
        return;
    }

    bool isLoop = true;
    do {
        auto entry = mIoQueue[type].Top();
        if (mCurrentQuota[type] >= entry->size) {
            mCurrentQuota[type] -= entry->size;
            mConcur[type]++;
            entry->Wake();
            mIoQueue[type].Pop();
        } else {
            isLoop = false;
        }
    } while (isLoop);
}
