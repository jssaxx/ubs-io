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

#ifndef BOOSTIO_SLICE_OPERATOR_H
#define BOOSTIO_SLICE_OPERATOR_H

#include "bio_err.h"
#include "slice.h"

namespace ock {
namespace bio {
class SliceOperator {
    virtual BResult Copy(const SlicePtr &from, const SlicePtr &to) = 0;
    virtual BResult Copy(const char *from, const SlicePtr &to) = 0;
    virtual BResult Copy(const SlicePtr &from, char *to, uint32_t toLen) = 0;
    virtual BResult Copy(const char *from, uint64_t start, uint32_t len, const SlicePtr &to) = 0;
    virtual BResult GetSliceFromSliceIO(SlicePtr &partialSlice, const SlicePtr &WholeSlice, uint64_t offset,
        uint64_t length) = 0;
};
}
}

#endif // BOOSTIO_SLICE_OPERATOR_H
