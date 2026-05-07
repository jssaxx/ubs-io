/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef MMSCORE_MMS_TYPES_H
#define MMSCORE_MMS_TYPES_H

namespace ock {
namespace mms {
using MmsNodeId = uint16_t;
using MmsPtId = uint16_t;

constexpr uint16_t NO_MAX_VALUE16 = 0xFFFFUL;
constexpr uint32_t NO_MAX_VALUE32 = 0xFFFFFFFFUL;
constexpr uint64_t NO_MAX_VALUE64 = 0xFFFFFFFFFFFFFFFFULL;

constexpr uint32_t NO_4194304 = 4194304; // 4M
constexpr uint32_t NO_1048576 = 1048576; // 1M
constexpr uint32_t NO_65535 = 65535;
constexpr uint32_t NO_10240 = 10240;
constexpr uint32_t NO_10000 = 10000;
constexpr uint32_t NO_8192 = 8192;
constexpr uint32_t NO_7800 = 7800;
constexpr uint32_t NO_7201 = 7201;
constexpr uint32_t NO_4096 = 4096;
constexpr uint32_t NO_2048 = 2048;
constexpr uint32_t NO_1536 = 1536;
constexpr uint32_t NO_1024 = 1024;
constexpr uint32_t NO_1000 = 1000;
constexpr uint32_t NO_5000 = 5000;
constexpr uint32_t NO_50000 = 50000;
constexpr uint32_t NO_150000 = 150000;
constexpr uint32_t NO_600 = 600;
constexpr uint32_t NO_512 = 512;
constexpr uint32_t NO_500 = 500;
constexpr uint32_t NO_300 = 300;
constexpr uint32_t NO_256 = 256;
constexpr uint32_t NO_255 = 255;
constexpr uint32_t NO_128 = 128;
constexpr uint32_t NO_100 = 100;
constexpr uint32_t NO_90 = 90;
constexpr uint32_t NO_80 = 80;
constexpr uint32_t NO_70 = 70;
constexpr uint32_t NO_64 = 64;
constexpr uint32_t NO_60 = 60;
constexpr uint32_t NO_50 = 50;
constexpr uint32_t NO_45 = 45;
constexpr uint32_t NO_40 = 40;
constexpr uint32_t NO_32 = 32;
constexpr uint32_t NO_30 = 30;
constexpr uint32_t NO_24 = 24;
constexpr uint32_t NO_20 = 20;
constexpr uint32_t NO_16 = 16;
constexpr uint32_t NO_15 = 15;
constexpr uint32_t NO_10 = 10;
constexpr uint32_t NO_9 = 9;
constexpr uint32_t NO_8 = 8;
constexpr uint32_t NO_6 = 6;
constexpr uint32_t NO_5 = 5;
constexpr uint32_t NO_4 = 4;
constexpr uint32_t NO_3 = 3;
constexpr uint32_t NO_2 = 2;
constexpr uint32_t NO_1 = 1;
constexpr uint32_t NO_0 = 0;

constexpr uint64_t NO_U64_0 = 0;
constexpr uint64_t NO_U64_1 = 1;
constexpr uint8_t NO_U8_255 = 255;

constexpr uint32_t IP_SIZE = 32;
constexpr uint32_t DEVICE_SIZE = 16;

constexpr size_t IO_SIZE_4K = 4 * 1024;
constexpr size_t IO_SIZE_8K = 8 * 1024;
constexpr size_t IO_SIZE_10K = 10 * 1024;
constexpr size_t IO_SIZE_64K = 64 * 1024;
constexpr size_t IO_SIZE_128K = 128 * 1024;
constexpr size_t IO_SIZE_256K = 256 * 1024;
constexpr size_t IO_SIZE_1M = 1024 * 1024;
constexpr size_t IO_SIZE_2M = 2 * 1024 * 1024;
constexpr size_t IO_SIZE_4M = 4 * 1024 * 1024;
constexpr size_t IO_SIZE_32M = 32 * 1024 * 1024;
constexpr size_t IO_SIZE_1G = 1024 * 1024 * 1024;

constexpr uint32_t IO_RETRY_NUM = 3;
constexpr uint32_t IO_RETRY_INTERAL = 2;

constexpr uint16_t RETRY_COUNT = 5;
constexpr uint16_t RETRY_SLEEP = 2;
constexpr uint32_t MIN_NODES_NUM = 1;
constexpr uint32_t MAX_NODES_NUM = 8;
constexpr uint32_t MAX_GROUPS_NUM = 32;
constexpr uint32_t MAX_NUMAS_NUM = 4;
constexpr uint32_t MAX_HEAD_SIZE = 32;
constexpr uint32_t MAX_KEY_SIZE = 128;
constexpr uint32_t MIN_KEY_SIZE = NO_1;
constexpr uint32_t MIN_VALUE_SIZE = NO_1;

constexpr uint32_t SEQ_QUEUE_LEN = NO_16;
constexpr uint32_t SEQ_QUEUE_MASK = SEQ_QUEUE_LEN - NO_1;
constexpr uint32_t SEQ_QUEUE_LEN_T = SEQ_QUEUE_LEN * NO_2;
}
}


#endif // MMSCORE_MMS_TYPES_H

