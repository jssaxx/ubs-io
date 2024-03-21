/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_SLICE_OPERATOR_H
#define BOOSTIO_SLICE_OPERATOR_H

#include "bio_err.h"
#include "slice.h"

namespace ock {
namespace bio {
class SliceOperator {
    virtual BResult Copy(const SlicePtr &from, const SlicePtr &to) = 0;
    virtual BResult Copy(const char *from, const SlicePtr &to) = 0;
    virtual BResult Copy(const SlicePtr &from, char *to) = 0;
    virtual BResult Copy(const char *from, uint64_t start, uint32_t len, const SlicePtr &to) = 0;
};
}
}

#endif // BOOSTIO_SLICE_OPERATOR_H
