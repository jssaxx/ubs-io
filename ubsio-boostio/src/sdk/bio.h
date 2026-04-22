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

#ifndef BIO_H
#define BIO_H

#include <functional>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include "bio_cache_statistics.h"
#include "bio_c.h"

namespace ock {
namespace bio {
constexpr uint32_t BIO_IO_MAX_LEN = 4194304;

struct AsyncPutParam {
    BioAsyncPutCallback callback;
    void *context;
};

struct AsyncGetParam {
    const char *key;
    uint64_t offset;
    uint64_t length;
    char *value;
};

struct AsyncOpParam {
    BioGetCallbackFunc func;
    void *context;
};

struct LoadPara {
    const char *key;
    uint64_t offset;
    uint64_t length;
};

class Bio {
public:
    using LoadCallback = std::function<void(void *context, int32_t result)>;

    /**
     * @brief: Calculate location information
     *
     * @param[in]: objectId: object id
     * @param[out]: location: location info
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult CalculateLocation(uint64_t objectId, ObjLocation &location);

    /**
    * @brief: Calculate object location info
    *
    * @param[in]: locations: multiple key locations
    * @param[in]: count: keys: batch get key count
    * @param[out]: position: query result, 0-local, 1-remote
    * @return: void
    */
    CResult BatchGetPositions(ObjLocation *locations, uint32_t count, int32_t *position);

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
     * @brief: Batch Get value
     *
     * @param[in]: tenantId: tenant id
     * @param[in]: keys: multiple keys
     * @param[in]: offset: offsets of multiple keys
     * @param[in]: length : lengths of the get values
     * @param[in]: location : location info
     * @param[out]: valueAddrs : address of the values corresponding to multiple keys, need free
     * @param[out]: realLength : real length
     * @param[out]: results : result of getting multiple keys
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult BatchGet(const char **keys, const uint32_t count, uint64_t *offsets, uint64_t *lengths,
                     ObjLocation *locations, uintptr_t *valueAddrs,
                     uint64_t *realLengths, int32_t *results);

    /**
     * @brief: Batch Get locaL value
     *
     * @param[in]: keys: multiple keys
     * @param[in]: length : lengths of the get values
     * @param[in]: location : location info
     * @param[out]: valueAddrs : address of the values corresponding to multiple keys, need free
     * @param[out]: results : result of getting multiple keys
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult BatchGetLocal(const char **keys, const uint32_t count, uint64_t *lengths,
                               ObjLocation *locations, uintptr_t *valueAddrs, int32_t *results);


    CResult BatchGetRemote(const char **keys, const uint32_t count,
                                ObjLocation *locations, uintptr_t **memAddr, size_t **memSize,
                                uint32_t row, uint32_t col, uintptr_t *valueAddrs, int32_t *results);
    /**
     * @brief: Batch Get value
     *
     * @param[in]: tenantId: tenant id
     * @param[in]: keys: multiple keys
     * @param[in]: offset: offsets of multiple keys
     * @param[in]: length : lengths of the get values, the same shape of valueAddrs
     * @param[in]: location : location info
     * @param[out]: valueAddrs : hbm address of the values corresponding to multiple keys
        valueAddrs {
            {key1_npu_buflayer_0, key1_npu_buflayer_1, ..., {key1_npu_buflayer_N}},
            {key2_npu_buflayer_0, key2_npu_buflayer_1},..., {key2_npu_buflayer_N}},
            ...,
            {keyM_npu_buflayer_0, keyM_npu_buflayer_1},..., {keyM_npu_buflayer_N}}
        }
     * @param[out]: results : result of getting multiple keys
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult BatchGetWithHbm(const char **keys, const uint32_t count, uint64_t *offsets, ObjLocation *locations,
                            uint64_t **lengths, uintptr_t **valueAddrs, uint64_t **realLengths, int32_t *results);

    /**
     * @brief: release the address returned by batchget.
     *
     * @param[in]: tenantId: tenant id
     * @param[in]: valueAddrs: return value of the BioBatchGet method
     * @param[in]: count: number of addresses in valueAddrs
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    void BatchGetFree(uintptr_t *valueAddrs, const uint32_t count);

    /**
     * @brief: Get the address of an key on the disk
     *
     * @param[in]: keys: key array
     * @param[in]: locations: location info array
     * @param[in]: count: key numbers
     * @param[out]: infos: address of key
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult BatchGetKeyDiskAddr(const char **keys, ObjLocation *locations, const uint32_t count, KeyAddrInfo *infos);

    /**
     * @brief: Get value
     *
     * @param[in]: param: get param info
     * @param[in]: location : location info
     * @param[out]: opParam : async op param
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult AsyncGet(AsyncGetParam param, const ObjLocation &location, AsyncOpParam &opParam);

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
     * @param[in]: para.key: object key
     * @param[in]: para.offset: offset
     * @param[in]: para.length: length
     * @param[in]: location: object location info
     * @param[in]: callback: callback function
     * @param[in]: context: callback context
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult Load(LoadPara &para, const ObjLocation location, const BioLoadCallback callback, void *context);

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

    /**
     * @brief: Batch exist object
     *
     * @param[in]: tenantId: tenant id
     * @param[in]: key[]: key array
     * @param[in]: location[] : location info array
     * @param[in]: count : key count
     * @param[out]: result[]: exist result
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult BatchExist(const char *key[], ObjLocation location[], uint32_t count, bool result[]);

    /**
     * @brief: Notify update finished
     *
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult NotifyUpdateFinish();

    /**
     * @brief: Notify prepare for update
     *
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult NotifyUpdatePrepare();

     /**
     * @brief: Notify prepare for update
     *
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult CheckUpdateReady();

    /**
     * @brief: alloc write space
     *
     * @param[in]: objectId: object id for alloc space
     * @param[in]: length : alloc space length
     * @param[out]: spaceInfo: alloc space info
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult AllocSpace(uint64_t objectId, uint64_t length, CacheSpaceDesc &spaceInfo);

    /**
     * @brief: put for copy free
     *
     * @param[in]: key: put key
     * @param[in]: spaceInfo : alloc space info
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult Put(const char *key, CacheSpaceDesc &spaceInfo);

    /**
     * @brief: Async put value
     *
     * @param[in]: key: object key
     * @param[in]: value: object value
     * @param[in]: length: value length
     * @param[in]: location: location info
     * @param[in]: param: callback param
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    CResult AsyncPut(const char *key, const char *value, uint64_t length, const ObjLocation &location,
        AsyncPutParam &param);

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
     * @param[in]: option: log and security options
     * @return: return initialize result
     */
    static CResult Initialize(WorkerMode mode, const ClientOptionsConfig &optConf, int32_t devId);

    /**
     * @brief: Exit bio service
     *
     * @return: void
     */
    static void Exit();

    /**
     * @brief: Show cache resource information
     *
     * @param[out]: CacheDescription: Cache Resource description array
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    static CResult BioShowCacheResource(std::vector<CacheResourcesDesc> &nodeDesc);

    /**
     * @brief: Show cache hit ratio information
     *
     * @param[out]: nodeDesc: Cache hit count information array
     * @return: return RETURN_CACHE_OK mean success, others, return non-zero value
     */
    static CResult BioShowCacheHitRatio(std::unordered_map<uint16_t, CacheHitDesc> &nodeDesc);

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