/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
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
    BResult Copy(const SlicePtr &from, char *to) override;
    BResult Copy(const char *from, uint64_t start, uint32_t len, const SlicePtr &to) override;

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
