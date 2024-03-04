/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef BIO_H
#define BIO_H

#include <functional>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include "bio_c.h"

namespace ock {
namespace bio {
constexpr uint32_t BIO_IO_MAX_LEN = 4194304;

class Bio {
public:
    using LoadCallback = std::function<void(void *context, CResult result)>;

    /**
     * @brief: Calculate location information
     *
     * @param[in]: objectId: object id
     * @param[out]: location: location info
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult CalculateLocation(uint64_t objectId, ObjLocation &location);

    /**
     * @brief: Put value
     *
     * @param[in]: key: object key
     * @param[in]: value: object value
     * @param[in]: length: value length
     * @param[in]: location: location info
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult Put(const char *key, const char *value, uint64_t length, const ObjLocation &location);

    /**
     * @brief: Get value
     *
     * @param[in]: key: object key
     * @param[in]: offset: offset of the get value
     * @param[in]: length : length of the get value
     * @param[in]: location : location info
     * @param[out]: value : object value
     * @param[out]: realLength : object value realLength
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult Get(const char *key, uint64_t offset, uint64_t length, const ObjLocation &location, char *value,
        uint64_t &realLength);

    /**
     * @brief: Delete key
     *
     * @param[in]: key: object key
     * @param[in]: location : location info
     * @return: return RETURN_CACHE_OK mean ok, others, return non-zero value
     */
    CResult Delete(const char *key, const ObjLocation &location);

    /**
     * @brief: Load value
     *
     * @param[in]: key: object key
     * @param[in]: offset: offset
     * @param[in]: length: length
     * @param[in]: location: object location info
     * @param[in]: callback: callback function
     * @param[in]: context: callback context
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult Load(const char *key, uint64_t offset, uint64_t length, const ObjLocation &location,
        const LoadCallback &callback, void *context);

    /**
     * @brief: List all key that meets the prefix condition
     *
     * @param[in]: prefix: Matching prefix
     * @param[out]: objs: Listed objects
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult ListAll(const char *prefix, std::unordered_map<std::string, ObjStat> &objs);

    /**
     * @brief: Get object stat info
     *
     * @param[in]: key: object key
     * @param[in]: location : location info
     * @param[out]: stat: object stat info
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult Stat(const char *key, const ObjLocation &location, ObjStat &stat);

    Bio(uint64_t id, AffinityStrategy affinity, WriteStrategy strategy)
        : mTenantId(id), mAffinity(affinity), mStrategy(strategy){};

    ~Bio() = default;

    uint64_t mTenantId = 0;                     // tenant id
    AffinityStrategy mAffinity = AFFINITY_BUTT; // cache affinity attribute
    WriteStrategy mStrategy = STRATEGY_BUTT;    // cache strategy
};

class BioService {
public:
    /**
     * @brief: Initialize bio service
     *
     * @param[in]: mode: boostio working mode
     * @return: return initialize result
     */
    static CResult Initialize(WorkerMode mode);

    /**
     * @brief: Exit bio service
     *
     * @return: void
     */
    static void Exit();

    /**
     * @brief: Create cache instance
     *
     * @param[in]: desc: cache descriptor
     * @return: return cache instance shared point
     */
    static std::shared_ptr<Bio> CreateCache(const CacheDescriptor &desc);

    /**
     * @brief: Get cache instance
     *
     * @param[in]: tenantId: tenant id
     * @return: return cache instance descriptor
     */
    static CacheDescriptor GetCache(uint64_t tenantId);

    /**
     * @brief: List bio instance
     *
     * @return: return all caches descriptor
     */
    static std::vector<CacheDescriptor> ListCache();

    /**
     * @brief: Destroy bio instance
     *
     * @param[in]: tenantId: tenant id
     * @return: void
     */
    static void DestroyCache(uint64_t tenantId);
};
}
}
#endif