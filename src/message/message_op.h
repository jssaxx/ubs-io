/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef MESSAGE_OP_H
#define MESSAGE_OP_H

#include <cstdint>

enum BioOpCode : uint16_t {
    BIO_OP_SDK_SHM_INIT = 0,
    BIO_OP_SDK_GET_NODE_INFO,
    BIO_OP_SDK_GET_NODE_VIEW,
    BIO_OP_SDK_QUERY_PT_VIEW,
    BIO_OP_SDK_PUT,
    BIO_OP_SDK_GET,
    BIO_OP_SDK_DELETE,
    BIO_OP_SDK_STAT,
    BIO_OP_SDK_LIST,
    BIO_OP_SDK_LOAD,
    BIO_OP_SDK_CREATE_FLOW,
    BIO_OP_SDK_GET_SLICE,
    BIO_OP_SDK_REPORT_HB,
    BIO_OP_SERVER_SYNC_DATA,
    BIO_OP_SERVER_GET_EVICT_OFFSET,
    BIO_OP_BUTT
};

#endif // MESSAGE_OP_H
