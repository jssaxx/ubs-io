/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#include "bio_qos.h"

using namespace ock::bio;

void BioQuota::Dispatch(QuotaType type)
{
    if (LIKELY(!mIoQueue[type].Empty())) {
        bool isLoop = true;
        do {
            auto entry = mIoQueue[type].Top();
            if ((mMaxQuota[type] - mAllocQuota[type]) >= entry->size) {
                mAllocQuota[type] += entry->size;
                entry->Wake();
                mIoQueue[type].Pop();
            } else {
                isLoop = false;
            }
        } while (isLoop);
    }
}
