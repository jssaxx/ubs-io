#ifndef BOOSTIO_INTERCEPTOR_READ_INDEX_H
#define BOOSTIO_INTERCEPTOR_READ_INDEX_H

#include <cstdint>
#include <cstddef>
#include "message.h"

namespace ock {
namespace bio {

constexpr uint64_t INTERCEPTOR_READ_INDEX_MAGIC = 0x5249445849445831ULL;
constexpr uint32_t INTERCEPTOR_READ_INDEX_VERSION = 2;
constexpr uint32_t INTERCEPTOR_READ_INDEX_ENTRY_BITS = 18U;
constexpr uint32_t INTERCEPTOR_READ_INDEX_ENTRY_COUNT = 1U << INTERCEPTOR_READ_INDEX_ENTRY_BITS;
constexpr uint32_t INTERCEPTOR_READ_INDEX_BUCKET_WAY = 4U;
constexpr uint64_t INTERCEPTOR_HASH_GOLDEN_RATIO = 0x9e3779b97f4a7c15ULL;
constexpr uint64_t INTERCEPTOR_HASH_MIX_1 = 0xff51afd7ed558ccdULL;
constexpr uint64_t INTERCEPTOR_HASH_MIX_2 = 0xc4ceb9fe1a85ec53ULL;
constexpr uint32_t INTERCEPTOR_HASH_LEFT_SHIFT = 6U;
constexpr uint32_t INTERCEPTOR_HASH_RIGHT_SHIFT = 2U;
constexpr uint32_t INTERCEPTOR_HASH_AVALANCHE_SHIFT = 33U;
constexpr uint32_t INTERCEPTOR_HASH_HIGH_SHIFT = 32U;

struct InterceptorReadIndexHeader {
    uint64_t magic;
    uint32_t version;
    uint32_t entryCount;
    uint32_t entrySize;
    uint32_t reserved;
};

struct InterceptorReadIndexEntry {
    uint64_t seq;
    uint64_t inode;
    uint64_t fileOffset;
    uint64_t dataLen;
    uint32_t addrNum;
    uint32_t reserved;
    uint64_t addrOffset[SLICE_ADDR_SIZE];
    uint64_t addrLen[SLICE_ADDR_SIZE];
};

struct InterceptorReadIndexCache {
    InterceptorReadIndexEntry *entry;
    uint64_t seq;
    uint64_t inode;
    uint64_t fileOffset;
    uint64_t dataLen;
    uint32_t addrNum;
    uint32_t reserved;
    uint64_t addrOffset[SLICE_ADDR_SIZE];
    uint64_t addrLen[SLICE_ADDR_SIZE];
};

inline uint64_t InterceptorReadIndexBlockOffset(uint64_t offset)
{
    return offset / MAX_INTERCEPTOR_IO_SIZE * MAX_INTERCEPTOR_IO_SIZE;
}

inline uint64_t InterceptorReadIndexHash(uint64_t inode, uint64_t blockOffset)
{
    uint64_t value = inode ^ (blockOffset + INTERCEPTOR_HASH_GOLDEN_RATIO + (inode << INTERCEPTOR_HASH_LEFT_SHIFT) +
        (inode >> INTERCEPTOR_HASH_RIGHT_SHIFT));
    value ^= value >> INTERCEPTOR_HASH_AVALANCHE_SHIFT;
    value *= INTERCEPTOR_HASH_MIX_1;
    value ^= value >> INTERCEPTOR_HASH_AVALANCHE_SHIFT;
    value *= INTERCEPTOR_HASH_MIX_2;
    value ^= value >> INTERCEPTOR_HASH_AVALANCHE_SHIFT;
    return value;
}

inline uint32_t InterceptorReadIndexBucketCount(uint32_t entryCount = INTERCEPTOR_READ_INDEX_ENTRY_COUNT)
{
    return entryCount / INTERCEPTOR_READ_INDEX_BUCKET_WAY;
}

inline uint32_t InterceptorReadIndexBucketBase(uint64_t hash, uint32_t entryCount = INTERCEPTOR_READ_INDEX_ENTRY_COUNT)
{
    uint32_t bucketCount = InterceptorReadIndexBucketCount(entryCount);
    if (bucketCount == 0) {
        return 0;
    }
    return static_cast<uint32_t>(hash % bucketCount) * INTERCEPTOR_READ_INDEX_BUCKET_WAY;
}

inline size_t InterceptorReadIndexLength(uint32_t entryCount = INTERCEPTOR_READ_INDEX_ENTRY_COUNT)
{
    return sizeof(InterceptorReadIndexHeader) + static_cast<size_t>(entryCount) * sizeof(InterceptorReadIndexEntry);
}

}
}

#endif
