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

#ifndef BOOSTIO_CACHE_SLICE_OPERATOR_H
#define BOOSTIO_CACHE_SLICE_OPERATOR_H

#include "slice_operator.h"

namespace ock {
namespace bio {
class CacheSliceOperator : public SliceOperator {
public:
    BResult Copy(const SlicePtr &from, const SlicePtr &to) override;
    BResult Copy(const char *from, const SlicePtr &to) override;
    BResult Copy(const SlicePtr &from, char *to, uint32_t toLen) override;
    BResult Copy(const char *from, uint64_t start, uint32_t len, const SlicePtr &to) override;
    BResult GetSliceFromSliceIO(SlicePtr &partialSlice, const SlicePtr &wholeSlice, uint64_t offset,
        uint64_t length) override;

private:
    static bool Validate(const SlicePtr &from, const SlicePtr &to);
    static bool Validate(const SlicePtr &slice);
    static BResult CopyFromDiskToDisk(const SlicePtr &from, const SlicePtr &to);
    static BResult CopyFromDiskToMemory(const SlicePtr &from, const SlicePtr &to);
    static BResult CopyFromMemoryToDisk(const SlicePtr &from, const SlicePtr &to);
    static BResult CopyFromMemoryToMemory(const SlicePtr &from, const SlicePtr &to);
    static uint64_t MinLen(uint64_t from, uint64_t to);
};
}
}


#endif // BOOSTIO_CACHE_SLICE_OPERATOR_H
