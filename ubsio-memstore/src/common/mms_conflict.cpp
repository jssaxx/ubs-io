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

#include "mms_conflict.h"

namespace ock {
namespace mms {

BResult MmsConflict::ApplyForTicket(uint32_t hashVal)
{
    uint32_t idx = hashVal & HASH_BUCKET_MASK;

    mLock[idx].LockWrite();
    auto iter = mHash[idx].find(hashVal);
    if (iter != mHash[idx].end()) {
        mLock[idx].UnLock();
        return MMS_INNER_RETRY;
    }
    mHash[idx][hashVal] = 0;
    mLock[idx].UnLock();

    return MMS_OK;
}

void MmsConflict::ReleaseTicket(uint32_t hashVal)
{
    uint32_t idx = hashVal & HASH_BUCKET_MASK;

    mLock[idx].LockWrite();
    mHash[idx].erase(hashVal);
    mLock[idx].UnLock();

    return;
}
}
}

