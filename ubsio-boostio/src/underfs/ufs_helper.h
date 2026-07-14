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
#include <unordered_map>
#include <memory>
#include <functional>
#include <dlfcn.h>
#include "bio_err.h"
#include "underfs_c.h"

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
            if (mConfig.underFsType == "none") {
                return BIO_OK;
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
            UnderFsConfigInfo underFsConfigInfo;
            underFsConfigInfo.underFsType = config.underFsType.c_str();
            underFsConfigInfo.cephConfig.cfgPath = config.cephConfig.cfgPath.c_str();
            underFsConfigInfo.cephConfig.cluster = config.cephConfig.cluster.c_str();
            underFsConfigInfo.cephConfig.user = config.cephConfig.user.c_str();
            underFsConfigInfo.cephConfig.poolName = config.cephConfig.pools.at(0).c_str();
            underFsConfigInfo.hdfsConfig.nameNode = config.hdfsConfig.nameNode.c_str();
            underFsConfigInfo.hdfsConfig.workingPath = config.hdfsConfig.workingPath.c_str();
            initUnderFsConfigOp(underFsConfigInfo);
            auto ret = initOp();
            if (ret != BIO_OK) {
                LOG_ERROR("Failed to init underfs, ret:" << ret << ".");
                return ret;
            }
            LOG_DEBUG("UfsHelper init success");
            return BIO_OK;
        }

        void Stop()
        {
            if (mConfig.underFsType == "none") {
                return;
            }
            stopOp();
        }

        BResult Put(const char *key, const char *value, const size_t len)
        {
            auto ret = putOp(key, value, len);
            if (UNLIKELY(ret != BIO_OK)) {
                LOG_ERROR("UfsHelper put failed, ret: " << ret << ", key: " << key << ", len: " << len << ".");
                return ret;
            }
            LOG_DEBUG("UfsHelper put success, key: " << key << ", len: " << len << ".");
            return ret;
        }

        BResult Get(const char *key, char *value, const size_t len, const uint64_t off)
        {
            auto ret = getOp(key, value, len, off);
            if (UNLIKELY(ret != BIO_OK)) {
                LOG_ERROR("UfsHelper get failed, ret" << ret << ", key: " << key << ", len: " <<
                                                      len << ", offset" << off << ".");
                return ret;
            }
            LOG_DEBUG("UfsHelper get success, key: " << key << ", len: " << len << ", offset" << off << ".");
            return ret;
        }

        BResult Delete(const char *key)
        {
            auto ret = deleteOp(key);
            if (UNLIKELY(ret != BIO_OK)) {
                LOG_ERROR("UfsHelper delete failed, ret" << ret << ", key: " << key << ".");
                return ret;
            }
            LOG_DEBUG("UfsHelper delete success, key: " << key << ".");
            return ret;
        }

        BResult Stat(const char *key, ObjStat &objStat)
        {
            ObjStatInfo statInfo;
            auto ret = statOp(key, &statInfo);
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
            auto ret = listOp(prefix, &listObjs, &size);
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
        void *LoadFunction(const char *name)
        {
            void *ptr = nullptr;
            ptr = dlsym(handler, name);
            if (ptr == nullptr) {
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
            if ((getOp = reinterpret_cast<GetFuncPtr>(LoadFunction("UfsGet"))) == nullptr) {
                return BIO_INNER_ERR;
            }
            if ((deleteOp = reinterpret_cast<DeleteFuncPtr>(LoadFunction("UfsDelete"))) == nullptr) {
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
        using GetFuncPtr = int (*)(const char *, char *, const size_t, const uint64_t);
        using DeleteFuncPtr = int (*)(const char *);
        using StatFuncPtr = int32_t (*)(const char *, ObjStatInfo *);
        using ListFuncPtr = int (*)(const char *, ObjStatInfo **, int *);
        using InitUnderFsConfigFuncPtr = void (*)(UnderFsConfigInfo);

        void *handler = nullptr;
        InitFuncPtr initOp = nullptr;
        StopFuncPtr stopOp = nullptr;
        PutFuncPtr putOp = nullptr;
        GetFuncPtr getOp = nullptr;
        DeleteFuncPtr deleteOp = nullptr;
        StatFuncPtr statOp = nullptr;
        ListFuncPtr listOp = nullptr;
        InitUnderFsConfigFuncPtr  initUnderFsConfigOp = nullptr;

        BioConfig::UnderFsConfig mConfig;

        DEFINE_REF_COUNT_VARIABLE;
    };
}
}
#endif // BOOSTIO_UFS_HELPER_H
