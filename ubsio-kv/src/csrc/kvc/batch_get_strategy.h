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

#ifndef BATCH_GET_STRATEGY_H
#define BATCH_GET_STRATEGY_H

#include <cstdint>
#include "bio_c.h"

namespace ock {
namespace ubsio {

struct BatchGetContext {
    uint64_t tenantId;
    const char **keys;
    uint32_t count;
    uint64_t *offsets;
    uint64_t *lengths;
    ObjLocation *locations;
    void **userBufs;
    uint64_t *realLengths;
    int32_t *results;
};

class BatchGetStrategy {
public:
    virtual ~BatchGetStrategy() = default;

    virtual int32_t Execute(const BatchGetContext &ctx) = 0;
};

class BatchGetStandalone : public BatchGetStrategy {
public:
    int32_t Execute(const BatchGetContext &ctx) override;
};

class BatchGetSeparates : public BatchGetStrategy {
public:
    int32_t Execute(const BatchGetContext &ctx) override;
};

} // namespace ubsio
} // namespace ock
#endif // BATCH_GET_STRATEGY_H
