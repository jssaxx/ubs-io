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

#ifndef MMS_CONFLICT_H
#define MMS_CONFLICT_H

#include <unordered_map>
#include "mms_ref.h"
#include "mms_err.h"
#include "mms_lock.h"

namespace ock {
namespace mms {

constexpr uint32_t HASH_BUCKET_NUM = 4096;
constexpr uint32_t HASH_BUCKET_MASK = HASH_BUCKET_NUM - 1;

class MmsConflict;
using MmsConflictPtr = Ref<MmsConflict>;
class MmsConflict {
public:
    MmsConflict() = default;
    ~MmsConflict() = default;

    inline static MmsConflictPtr &Instance()
    {
        static auto instance = MakeRef<MmsConflict>();
        return instance;
    }

    BResult ApplyForTicket(uint32_t hashVal);

    void ReleaseTicket(uint32_t hashVal);

    DEFINE_REF_COUNT_FUNCTIONS;

private:
    std::unordered_map<uint32_t, uint32_t> mHash[HASH_BUCKET_NUM];
    ReadWriteLock mLock[HASH_BUCKET_NUM];

    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif

