/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_BIO_TYPES_H
#define BOOSTIO_BIO_TYPES_H

namespace ock {
namespace bio {
using BioNodeId = uint32_t;

constexpr uint32_t NO_MAX_VALUE32 = 0xFFFFFFFFUL;
constexpr uint64_t NO_MAX_VALUE64 = 0xFFFFFFFFFFFFFFFFULL;

constexpr uint32_t NO_4194304 = 4194304; // 4M
constexpr uint32_t NO_65535 = 65535;
constexpr uint32_t NO_10240 = 10240;
constexpr uint32_t NO_8192 = 8192;
constexpr uint32_t NO_7800 = 7800;
constexpr uint32_t NO_7201 = 7201;
constexpr uint32_t NO_4096 = 4096;
constexpr uint32_t NO_2048 = 2048;
constexpr uint32_t NO_1536 = 1536;
constexpr uint32_t NO_1024 = 1024;
constexpr uint32_t NO_1000 = 1000;
constexpr uint32_t NO_600 = 600;
constexpr uint32_t NO_512 = 512;
constexpr uint32_t NO_300 = 300;
constexpr uint32_t NO_256 = 256;
constexpr uint32_t NO_255 = 255;
constexpr uint32_t NO_128 = 128;
constexpr uint32_t NO_100 = 100;
constexpr uint32_t NO_64 = 64;
constexpr uint32_t NO_60 = 60;
constexpr uint32_t NO_32 = 32;
constexpr uint32_t NO_24 = 24;
constexpr uint32_t NO_16 = 16;
constexpr uint32_t NO_15 = 15;
constexpr uint32_t NO_10 = 10;
constexpr uint32_t NO_8 = 8;
constexpr uint32_t NO_5 = 5;
constexpr uint32_t NO_4 = 4;
constexpr uint32_t NO_3 = 3;
constexpr uint32_t NO_2 = 2;
constexpr uint32_t NO_1 = 1;

constexpr uint64_t NO_U64_0 = 0;
constexpr uint64_t NO_U64_1 = 1;

constexpr uint32_t IP_SIZE = 32;
}
}


#endif // BOOSTIO_BIO_TYPES_H
