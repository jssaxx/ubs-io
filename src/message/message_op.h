/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef MESSAGE_OP_H
#define MESSAGE_OP_H

#include <cstdint>

enum BioOpCode : uint16_t {
    BIO_OP_SDK_QUERY_PT_VIEW = 0,
    BIO_OP_SDK_PUT = 1,
    BIO_OP_SDK_GET = 2,
    BIO_OP_SDK_DELETE = 3,
    BIO_OP_SDK_STAT = 4,
    BIO_OP_SDK_LOAD = 5,
    BIO_OP_SDK_CREATE_FLOW = 6,

    BIO_OP_SERVER_SYNC_DATA,
    BIO_OP_BUTT
};

#endif // MESSAGE_OP_H
