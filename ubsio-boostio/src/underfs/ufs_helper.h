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

#ifndef BOOSTIO_UFS_HELPER_H
#define BOOSTIO_UFS_HELPER_H

#include <string>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>
#include <dlfcn.h>
#include "bio_err.h"
#include "underfs_c.h"
#include "bio_trace.h"
#include "bio_meta_event.h"

namespace ock {
namespace bio {
    class UfsHelper;
    using UfsHelperPtr = Ref<UfsHelper>;
    class UfsHelper {
    public:
        struct ObjStat {
            uint64_t size;
            time_t time;
        };

        inline static UfsHelperPtr &Instance()
        {
            static auto instance = MakeRef<UfsHelper>();
            return instance;
        }

        const BioConfig::UnderFsConfig &GetConfig()
        {
            return mConfig;
        }

        BResult Initialize(BioConfig::UnderFsConfig config)
        {
            mConfig = config;
            if (mConfig.underFsType == BIO_UNDERFS_NONE) {
                return BIO_OK;
            }
            const char *typeName = UnderFsTypeName(config.underFsType);
            if (typeName == nullptr) {
                return BIO_INVALID_PARAM;
            }
            const char *soFileName = "libbio_underfs.so";
            handler = dlopen(soFileName, RTLD_NOW);
            if (handler == nullptr) {
                LOG_ERROR("Failed to open library: " << soFileName << " , dlopen error: " << dlerror() << ".");
                return BIO_UFS_IOERR;
            }

            if (InitOperations() != BIO_OK) {
                LOG_ERROR("Failed to init operations.");
                return BIO_UFS_IOERR;
            }
            UnderFsConfigInfo underFsConfigInfo{};
            underFsConfigInfo.underFsType = typeName;
            underFsConfigInfo.cephConfig.cfgPath = config.cephConfig.cfgPath.c_str();
            underFsConfigInfo.cephConfig.cluster = config.cephConfig.cluster.c_str();
            underFsConfigInfo.cephConfig.user = config.cephConfig.user.c_str();
            underFsConfigInfo.cephConfig.poolName = config.cephConfig.pools.at(0).c_str();
            underFsConfigInfo.hdfsConfig.nameNode = config.hdfsConfig.nameNode.c_str();
            underFsConfigInfo.hdfsConfig.workingPath = config.hdfsConfig.workingPath.c_str();
            underFsConfigInfo.localConfig.rootPath = config.localConfig.rootPath.c_str();
            underFsConfigInfo.localConfig.slotIndex = config.localConfig.slotIndex;
            underFsConfigInfo.localConfig.deviceCount = config.localConfig.deviceCount;
            underFsConfigInfo.localConfig.evictionPolicy = FileEvictionPolicyName(config.localConfig.evictionPolicy);
            underFsConfigInfo.localConfig.checkIntervalSeconds = config.localConfig.checkIntervalSeconds;
            initUnderFsConfigOp(underFsConfigInfo);
            if (config.underFsType == BIO_UNDERFS_LOCAL) {
                auto result = InitLocalExtensions();
                if (UNLIKELY(result != BIO_OK)) {
                    return result;
                }
            }
            auto ret = initOp();
            if (ret != BIO_OK) {
                LOG_ERROR("Failed to init underfs, ret:" << ret << ".");
                if (config.underFsType == BIO_UNDERFS_LOCAL) {
                    Stop();
                }
                return ret;
            }
            LOG_DEBUG("UfsHelper init success");
            return BIO_OK;
        }

        void Stop()
        {
            if (mConfig.underFsType == BIO_UNDERFS_NONE) {
                return;
            }
            stopOp();
            std::lock_guard<std::mutex> lock(mLocalMetaMutex);
            mLocalMetaEventCallback = nullptr;
            std::vector<UbsIoMetaEvent>().swap(mPendingLocalMetaEvents);
        }

        // Replaces the callback; queued events are delivered when a callback is registered.
        void RegLocalMetaEventCallback(UbsIoMetaEventCallback callback) noexcept
        {
            std::lock_guard<std::mutex> lock(mLocalMetaMutex);
            mLocalMetaEventCallback = std::move(callback);
            if (mLocalMetaEventCallback != nullptr) {
                if (!mPendingLocalMetaEvents.empty()) {
                    mLocalMetaEventCallback(mPendingLocalMetaEvents);
                }
                std::vector<UbsIoMetaEvent>().swap(mPendingLocalMetaEvents);
            }
        }

        BResult Put(const char *key, const char *value, const size_t len)
        {
            BIO_TRACE_START(UFS_TRACE_PUT);
            auto ret = putOp(key, value, len);
            BIO_TRACE_END(UFS_TRACE_PUT, ret);
            if (UNLIKELY(ret != BIO_OK)) {
                LOG_ERROR("UfsHelper put failed, ret: " << ret << ", key: " << key << ", len: " << len << ".");
                return ret;
            }
            LOG_DEBUG("UfsHelper put success, key: " << key << ", len: " << len << ".");
            return ret;
        }

        bool SupportsPutV() const noexcept
        {
            return mConfig.underFsType == BIO_UNDERFS_LOCAL || mConfig.underFsType == BIO_UNDERFS_SHARED;
        }

        BResult PutV(const char *key, const struct iovec *iov, size_t iovcnt) noexcept
        {
            if (UNLIKELY(!SupportsPutV())) {
                return BIO_INVALID_PARAM;
            }
            BIO_TRACE_START(UFS_TRACE_PUT);
            auto ret = putVOp(key, iov, iovcnt);
            BIO_TRACE_END(UFS_TRACE_PUT, ret);
            if (UNLIKELY(ret != BIO_OK)) {
                LOG_ERROR("UnderFS vectored put failed, ret:" << ret << ", vectors:" << iovcnt << ".");
            }
            return ret;
        }

        BResult Get(const char *key, char *value, const size_t len, const uint64_t off)
        {
            BIO_TRACE_START(UFS_TRACE_GET);
            auto ret = getOp(key, value, len, off);
            BIO_TRACE_END(UFS_TRACE_GET, ret);
            if (UNLIKELY(ret != BIO_OK)) {
                LOG_ERROR("UfsHelper get failed, ret" << ret << ", key: " << key << ", len: " <<
                                                      len << ", offset" << off << ".");
                return ret;
            }
            LOG_DEBUG("UfsHelper get success, key: " << key << ", len: " << len << ", offset" << off << ".");
            return ret;
        }

        BResult GetWithRealLen(const char *key, char *value, const size_t len, const uint64_t off, size_t &realLen)
        {
            BIO_TRACE_START(UFS_TRACE_GET);
            auto ret = getWithRealLenOp(key, value, len, off, &realLen);
            BIO_TRACE_END(UFS_TRACE_GET, ret);
            if (UNLIKELY(ret != BIO_OK)) {
                LOG_ERROR("UfsHelper get with real length failed, ret:" << ret << ", key:" << key <<
                    ", len:" << len << ", offset:" << off << ".");
                return ret;
            }
            LOG_DEBUG("UfsHelper get with real length success, key:" << key << ", len:" << len <<
                ", realLen:" << realLen << ", offset:" << off << ".");
            return ret;
        }

        BResult GetStorageMetrics(UfsStorageMetrics &metrics) noexcept
        {
            if (UNLIKELY(mConfig.underFsType != BIO_UNDERFS_LOCAL)) {
                return BIO_INVALID_PARAM;
            }
            if (UNLIKELY(getStorageMetricsOp == nullptr)) {
                return BIO_NOT_READY;
            }
            return getStorageMetricsOp(&metrics);
        }

        BResult ScanLocalKeys(std::unordered_map<std::string, uint64_t> &items) noexcept
        {
            if (UNLIKELY(mConfig.underFsType != BIO_UNDERFS_LOCAL)) {
                return BIO_INVALID_PARAM;
            }
            if (UNLIKELY(scanLocalKeysOp == nullptr)) {
                return BIO_NOT_READY;
            }
            UfsLocalKeyInfo *snapshot = nullptr;
            uint64_t count = 0;
            BResult ret = scanLocalKeysOp(&snapshot, &count);
            std::unique_ptr<UfsLocalKeyInfo, decltype(&std::free)> result(snapshot, &std::free);
            if (UNLIKELY(ret != BIO_OK)) {
                return ret;
            }
            for (uint64_t i = 0; i < count; ++i) {
                items.emplace(snapshot[i].key, snapshot[i].valueLen);
            }
            return BIO_OK;
        }

        BResult Delete(const char *key)
        {
            BIO_TRACE_START(UFS_TRACE_DEL);
            auto ret = deleteOp(key);
            BIO_TRACE_END(UFS_TRACE_DEL, ret);
            if (UNLIKELY(ret != BIO_OK)) {
                LOG_ERROR("UfsHelper delete failed, ret" << ret << ", key: " << key << ".");
                return ret;
            }
            LOG_DEBUG("UfsHelper delete success, key: " << key << ".");
            return ret;
        }

        BResult Exist(const char *key)
        {
            BIO_TRACE_START(UFS_TRACE_EXIST);
            BResult ret = existOp(key);
            BIO_TRACE_END(UFS_TRACE_EXIST, ret);
            if (UNLIKELY(ret != BIO_OK && ret != BIO_NOT_EXISTS)) {
                LOG_ERROR("UfsHelper exist failed, ret:" << ret << ", key:" << key << ".");
            }
            return ret;
        }

        BResult Stat(const char *key, ObjStat &objStat)
        {
            ObjStatInfo statInfo;
            BIO_TRACE_START(UFS_TRACE_STAT);
            auto ret = statOp(key, &statInfo);
            BIO_TRACE_END(UFS_TRACE_STAT, ret);
            if (UNLIKELY(ret != BIO_OK)) {
                LOG_ERROR("UfsHelper stat failed, ret: " << ret << ", key: " << key << ".");
                return ret;
            }
            objStat.size = statInfo.size;
            objStat.time = statInfo.time;
            LOG_DEBUG("UfsHelper stat success, key: " << key << ".");
            return ret;
        }

        BResult List(const char *prefix, std::unordered_map<std::string, ObjStat> &objStat)
        {
            ObjStatInfo *listObjs;
            int size = 0;
            BIO_TRACE_START(UFS_TRACE_LIST);
            auto ret = listOp(prefix, &listObjs, &size);
            BIO_TRACE_END(UFS_TRACE_LIST, ret);
            if (UNLIKELY(ret != BIO_OK)) {
                LOG_ERROR("UfsHelper list failed, ret" << ret << ", prefix: " << prefix << ".");
                return ret;
            }
            for (int i = 0; i < size; ++i) {
                objStat.emplace(std::make_pair<std::string, ObjStat>(
                    listObjs[i].key, { listObjs[i].size, listObjs[i].time }));
            }
            free(listObjs);
            listObjs = nullptr;
            LOG_DEBUG("UfsHelper list success, prefix: " << prefix << ".");
            return BIO_OK;
        }

        DEFINE_REF_COUNT_FUNCTIONS;

    private:
        BResult InitLocalExtensions() noexcept
        {
            if ((getStorageMetricsOp = reinterpret_cast<GetStorageMetricsFuncPtr>(
                LoadFunction("UfsGetStorageMetrics"))) == nullptr) {
                return BIO_NOT_READY;
            }
            if ((scanLocalKeysOp = reinterpret_cast<ScanLocalKeysFuncPtr>(LoadFunction("UfsScanLocalKeys"))) ==
                nullptr) {
                return BIO_NOT_READY;
            }
            using SetCallback = int32_t (*)(UfsLocalMetaEventCallback, void *);
            auto setCallback = reinterpret_cast<SetCallback>(dlsym(handler, "UfsSetLocalMetaEventCallback"));
            if (UNLIKELY(setCallback == nullptr)) {
                LOG_ERROR("Local UnderFS requires metadata event support in libbio_underfs.so.");
                return BIO_NOT_READY;
            }
            return setCallback(&UfsHelper::ReceiveLocalMetaEvent, this);
        }

        static void ReceiveLocalMetaEvent(void *context, UfsLocalMetaEventType type, const char *key) noexcept
        {
            if (UNLIKELY(type != UFS_LOCAL_META_DELETE)) {
                return;
            }
            auto *self = static_cast<UfsHelper *>(context);
            std::lock_guard<std::mutex> lock(self->mLocalMetaMutex);
            if (self->mLocalMetaEventCallback == nullptr) {
                self->mPendingLocalMetaEvents.push_back({UBSIO_META_DELETE, key});
                return;
            }
            const std::vector<UbsIoMetaEvent> events{{UBSIO_META_DELETE, key}};
            self->mLocalMetaEventCallback(events);
        }

        void *LoadFunction(const char *name)
        {
            void *ptr = nullptr;
            ptr = dlsym(handler, name);
            if (UNLIKELY(ptr == nullptr)) {
                LOG_ERROR("Failed to load function " << name << ".");
                return nullptr;
            }
            return ptr;
        }

        BResult InitOperations()
        {
            if ((initOp = reinterpret_cast<InitFuncPtr>(LoadFunction("UfsInit"))) == nullptr) {
                return BIO_INNER_ERR;
            }
            if ((stopOp = reinterpret_cast<StopFuncPtr>(LoadFunction("UfsStop"))) == nullptr) {
                return BIO_INNER_ERR;
            }
            if ((putOp = reinterpret_cast<PutFuncPtr>(LoadFunction("UfsPut"))) == nullptr) {
                return BIO_INNER_ERR;
            }
            if ((putVOp = reinterpret_cast<PutVFuncPtr>(LoadFunction("UfsPutV"))) == nullptr) {
                return BIO_INNER_ERR;
            }
            if ((getOp = reinterpret_cast<GetFuncPtr>(LoadFunction("UfsGet"))) == nullptr) {
                return BIO_INNER_ERR;
            }
            if ((getWithRealLenOp = reinterpret_cast<GetWithRealLenFuncPtr>(LoadFunction("UfsGetWithRealLen"))) ==
                nullptr) {
                return BIO_INNER_ERR;
            }
            if ((deleteOp = reinterpret_cast<DeleteFuncPtr>(LoadFunction("UfsDelete"))) == nullptr) {
                return BIO_INNER_ERR;
            }
            if ((existOp = reinterpret_cast<ExistFuncPtr>(LoadFunction("UfsExist"))) == nullptr) {
                return BIO_INNER_ERR;
            }
            if ((statOp = reinterpret_cast<StatFuncPtr>(LoadFunction("UfsStat"))) == nullptr) {
                return BIO_INNER_ERR;
            }
            if ((listOp = reinterpret_cast<ListFuncPtr>(LoadFunction("UfsList"))) == nullptr) {
                return BIO_INNER_ERR;
            }
            if ((initUnderFsConfigOp = reinterpret_cast<InitUnderFsConfigFuncPtr>(
                LoadFunction("UfsInitUnderFsConfig"))) == nullptr) {
                return BIO_INNER_ERR;
            }
            return BIO_OK;
        }

        using InitFuncPtr = int (*)();
        using StopFuncPtr = void (*)();
        using PutFuncPtr = int (*)(const char *, const char *, size_t);
        using PutVFuncPtr = int32_t (*)(const char *, const struct iovec *, size_t);
        using GetFuncPtr = int (*)(const char *, char *, const size_t, const uint64_t);
        using GetWithRealLenFuncPtr = int (*)(const char *, char *, const size_t, const uint64_t, size_t *);
        using DeleteFuncPtr = int (*)(const char *);
        using ExistFuncPtr = int (*)(const char *);
        using StatFuncPtr = int32_t (*)(const char *, ObjStatInfo *);
        using ListFuncPtr = int (*)(const char *, ObjStatInfo **, int *);
        using InitUnderFsConfigFuncPtr = void (*)(UnderFsConfigInfo);
        using ScanLocalKeysFuncPtr = int32_t (*)(UfsLocalKeyInfo **, uint64_t *);
        using GetStorageMetricsFuncPtr = int32_t (*)(UfsStorageMetrics *);

        void *handler = nullptr;
        InitFuncPtr initOp = nullptr;
        StopFuncPtr stopOp = nullptr;
        PutFuncPtr putOp = nullptr;
        PutVFuncPtr putVOp = nullptr;
        GetFuncPtr getOp = nullptr;
        GetWithRealLenFuncPtr getWithRealLenOp = nullptr;
        DeleteFuncPtr deleteOp = nullptr;
        ExistFuncPtr existOp = nullptr;
        StatFuncPtr statOp = nullptr;
        ListFuncPtr listOp = nullptr;
        InitUnderFsConfigFuncPtr  initUnderFsConfigOp = nullptr;
        ScanLocalKeysFuncPtr scanLocalKeysOp = nullptr;
        GetStorageMetricsFuncPtr getStorageMetricsOp = nullptr;

        BioConfig::UnderFsConfig mConfig;
        std::mutex mLocalMetaMutex;
        std::vector<UbsIoMetaEvent> mPendingLocalMetaEvents;
        UbsIoMetaEventCallback mLocalMetaEventCallback{ nullptr };

        DEFINE_REF_COUNT_VARIABLE;
    };
}
}
#endif // BOOSTIO_UFS_HELPER_H
