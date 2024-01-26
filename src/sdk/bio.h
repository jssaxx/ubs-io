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

namespace ock {
namespace bio {
enum CResult : int32_t {
    RET_CACHE_OK = 0,            // successful
    RET_CACHE_PROTECTED = 1,     // cache write protected
    RET_CACHE_ERROR = 2,         // unknown error code
    RET_CACHE_EPERM = 3,         // input parameter is incorrect
    RET_CACHE_BUSY = 4,          // cache busy, need outer retry
    RET_CACHE_NEED_RETRY = 5,    // need retry
    RET_CACHE_NOT_READY = 6,     // retry is not required
    RET_CACHE_NOT_FOUND = 7,     // not found this key
    RET_CACHE_CONFLICT = 8,      // key conflict
    RET_CACHE_MISS = 9,          // cache miss
    RET_CACHE_NO_SPACE = 10,     // cache capacity not enough
    RET_CACHE_UNAVAILABLE = 11,  // cache service unavailable
    RET_CACHE_EXCEED_QUOTA = 12, // exceed cache quota limit
    RET_CACHE_PT_FAULT = 13,     // cache partition fault
    RET_CACHE_READ_EXCEED = 14,  // read limit is exceeded
    RET_CACHE_BUTT
};

enum AffinityStrategy {
    LOCAL_AFFINITY = 1, // data local affinity
    GLOBAL_BALANCE = 2, // data global balance
    AFFINITY_BUTT
};

enum WriteStrategy {
    WRITE_BACK = 1,
    WRITE_THROUGH = 2,
    STRATEGY_BUTT
};

class Bio {
public:
    struct ObjStat {
        uint32_t size; // value size
        time_t time;   // modify time;
    };

    struct ObjLocation {
        uint64_t location[2];
    };

    using LoadCallback = std::function<void(void *context, CResult result)>;

    /* *
     * @brief: Calculate location information
     *
     * @param[in]: objectId: object id
     * @param[out]: location: location info
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult CalculateLocation(uint64_t objectId, ObjLocation &location);

    /* *
     * @brief: Put value
     *
     * @param[in]: key: object key
     * @param[in]: value: object value
     * @param[in]: length: value length
     * @param[in]: location: location info
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult Put(const char *key, const char *value, uint64_t length, const ObjLocation &location);

    /* *
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
    CResult Get(const char *key, uint64_t offset, uint64_t length, const ObjLocation &location,
        char *value, uint64_t &realLength);

    /* *
     * @brief: Delete key
     *
     * @param[in]: key: object key
     * @param[in]: location : location info
     * @return: return RETURN_CACHE_OK mean ok, others, return non-zero value
     */
    CResult Delete(const char *key, const ObjLocation &location);

    /* *
     * @brief: Load value
     *
     * @param[in]: key: object key
     * @param[in]: location: object location info
     * @param[in]: callback: callback function
     * @param[in]: context: callback context
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult Load(const char *key, uint64_t offset, uint64_t length, const ObjLocation &location, const LoadCallback& callback, void *context);

    /* *
     * @brief: List all key that meets the prefix condition
     *
     * @param[in]: prefix: Matching prefix
     * @param[out]: objs: Listed objects
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult ListAll(const char *prefix, const std::vector<std::pair<char *, ObjStat>>& objs);

    /* *
     * @brief: Get object stat info
     *
     * @param[in]: key: object key
     * @param[in]: location : location info
     * @return: return object stat info
     */
    ObjStat Stat(const char *key, const ObjLocation &location);

    /* *
     * @brief: Get bio instance tenant id
     *
     * @return: tenant id
     */
    inline uint64_t GetTenantId() const
    {
        return mTenantId;
    }

    /* *
     * @brief: Get bio instance affinity policy
     *
     * @return: affinity policy
     */
    inline AffinityStrategy GetAffinityPolicy() const
    {
        return mAffinity;
    }

    /* *
     * @brief: Get bio instance write strategy
     *
     * @return: write strategy
     */
    inline WriteStrategy GetWriteStrategy() const
    {
        return mStrategy;
    }

    /* *
     * @brief: Get bio instance capacity
     *
     * @return: capacity
     */
    inline uint64_t GetCapacity() const
    {
        return mCapacity;
    }

    Bio(uint64_t id, AffinityStrategy affinity, WriteStrategy strategy, uint64_t cap)
        : mTenantId(id), mAffinity(affinity), mStrategy(strategy), mCapacity(cap){};
    ~Bio() = default;

private:
    uint64_t mTenantId = 0;                     // tenant id
    AffinityStrategy mAffinity = AFFINITY_BUTT; // cache affinity attribute
    WriteStrategy mStrategy = STRATEGY_BUTT;    // cache strategy
    uint64_t mCapacity = 0;                     // cache capacity
};

class BioService {
public:
    struct Descriptor {
        uint64_t tenantId;
        AffinityStrategy affinity;
        WriteStrategy strategy;
        uint64_t capacity;
    };

    /* *
     * @brief: Initialize bio service
     *
     * @param[in]: desc: cache descriptor
     * @return: return cache instance shared point
     */
    static CResult Initialize();

    /* *
     * @brief: Exit boostio bio service
     *
     * @param[in]: desc: cache descriptor
     * @return: return cache instance shared point
     */
    static void Exit();

    /* *
     * @brief: Create bio instance
     *
     * @param[in]: desc: cache descriptor
     * @return: return cache instance shared point
     */
    static std::shared_ptr<Bio> CreateCache(const Descriptor &desc);

    /* *
     * @brief: Get bio instance
     *
     * @param[in]: tenantId: tenant id
     * @return: return cache instance shared point
     */
    static std::shared_ptr<Bio> GetCache(uint64_t tenantId);

    /* *
     * @brief: List bio instance
     *
     * @return: return all caches
     */
    static std::unordered_map<uint64_t, std::shared_ptr<Bio>> ListCache();

    /* *
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