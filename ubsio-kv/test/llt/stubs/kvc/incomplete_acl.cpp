/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 */

#include <cstdint>

extern "C" int32_t aclrtGetDevice(int32_t *device)
{
    if (device != nullptr) {
        *device = 0;
    }
    return 0;
}
