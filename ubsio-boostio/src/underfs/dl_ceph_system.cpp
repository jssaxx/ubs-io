/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <cstring>
#include <dlfcn.h>
#include "bio_config_instance.h"
#include "bio_log.h"
#include "bio_trace.h"
#include "bio_tracepoint_helper.h"
#include "underfs_config.h"
#include "dl_ceph_system.h"

namespace ock {
namespace bio {

void *DlCephSystem::LoadFunction(const char *name)
{
    void *func = dlsym(mLibHandle, name);
    if (func == nullptr) {
        LOG_ERROR("Failed to load symbol " << name << ": " << dlerror());
    }
    return func;
}

BResult DlCephSystem::InitOperations()
{
    if ((mRadosCreate2 = reinterpret_cast<RadosCreate2Fn>(LoadFunction("rados_create2"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((mRadosConfReadFile = reinterpret_cast<RadosConfReadFileFn>(LoadFunction("rados_conf_read_file"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((mRadosShutdown = reinterpret_cast<RadosShutdownFn>(LoadFunction("rados_shutdown"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((mRadosConnect = reinterpret_cast<RadosConnectFn>(LoadFunction("rados_connect"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((mRadosPoolLookup = reinterpret_cast<RadosPoolLookupFn>(LoadFunction("rados_pool_lookup"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((mRadosPoolCreate = reinterpret_cast<RadosPoolCreateFn>(LoadFunction("rados_pool_create"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((mRadosIoCtxCreate = reinterpret_cast<RadosIoCtxCreateFn>(LoadFunction("rados_ioctx_create"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((mRadosIoCtxDestroy = reinterpret_cast<RadosIoCtxDestroyFn>(LoadFunction("rados_ioctx_destroy"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((mRadosWrite = reinterpret_cast<RadosWriteFn>(LoadFunction("rados_write"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((mRadosRead = reinterpret_cast<RadosReadFn>(LoadFunction("rados_read"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((mRadosRemove = reinterpret_cast<RadosRemoveFn>(LoadFunction("rados_remove"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((mRadosStat = reinterpret_cast<RadosStatFn>(LoadFunction("rados_stat"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((mRadosNobjectsListOpen = reinterpret_cast<RadosNobjectsListOpenFn>(
            LoadFunction("rados_nobjects_list_open"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((mRadosNobjectsListNext = reinterpret_cast<RadosNobjectsListNextFn>(
            LoadFunction("rados_nobjects_list_next"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((mRadosNobjectsListClose = reinterpret_cast<RadosNobjectsListCloseFn>(
            LoadFunction("rados_nobjects_list_close"))) == nullptr) {
        return BIO_INNER_ERR;
    }

    return BIO_OK;
}

BResult DlCephSystem::LoadCephLibrary()
{
#ifdef DEBUG_UT
    return BIO_OK;
#endif

    mLibHandle = dlopen("librados.so.2", RTLD_NOW);
    if (mLibHandle == nullptr) {
        LOG_ERROR("Failed to load librados.so.2: " << dlerror() <<
            ". Please install ceph-common or librados2 package.");
        return BIO_UFS_IOERR;
    }

    if (InitOperations() != BIO_OK) {
        LOG_ERROR("Failed to init ceph operations.");
        dlclose(mLibHandle);
        mLibHandle = nullptr;
        return BIO_UFS_IOERR;
    }

    return BIO_OK;
}

BResult DlCephSystem::Init()
{
    if (mInited) {
        return BIO_OK;
    }

    if (LoadCephLibrary() != BIO_OK) {
        LOG_ERROR("Failed to load ceph library.");
        return BIO_UFS_IOERR;
    }

    LoadCephConfig();
    int ret = BIO_UFS_IOERR;
    BIO_TP_START(UNDERFS_CEPH_CREAT_FAIL, &ret, -1);
    ret = mRadosCreate2(&mConn, mCluster.c_str(), mUser.c_str(), 0);
    BIO_TP_END;
    if (ret < 0 || mConn == nullptr) {
        LOG_ERROR("Failed to create, ret:" << ret);
        return BIO_UFS_IOERR;
    }
    ret = mRadosConfReadFile(mConn, mCfgPath.c_str());
    BIO_TP_START(UNDERFS_CEPH_READ_FILE_FAIL, &ret, -1);
    BIO_TP_END;
    if (ret < 0) {
        LOG_ERROR("Failed to read config, ret:" << ret);
        mRadosShutdown(mConn);
        return BIO_UFS_IOERR;
    }

    ret = mRadosConnect(mConn);
    BIO_TP_START(UNDERFS_CEPH_CONNECT_FAIL, &ret, -1);
    BIO_TP_END;
    if (ret < 0) {
        LOG_ERROR("Failed to connect, ret:" << ret);
        mRadosShutdown(mConn);
        return BIO_UFS_IOERR;
    }

    if (mRadosPoolLookup(mConn, mPool.c_str()) < 0) {
        if (mRadosPoolCreate(mConn, mPool.c_str()) < 0) {
            LOG_ERROR("Failed to create pool.");
            mRadosShutdown(mConn);
            return BIO_UFS_IOERR;
        }
    }

    ret = mRadosIoCtxCreate(mConn, mPool.c_str(), &mIoCtx);
    BIO_TP_START(UNDERFS_CEPH_IOCTX_CREAT_FAIL, &ret, -1);
    BIO_TP_END;
    if (ret < 0) {
        LOG_ERROR("Failed to create ioctx, ret:" << ret);
        mRadosShutdown(mConn);
        return BIO_UFS_IOERR;
    }

    LOG_INFO("UnderFS initialize succeed, cluster:" << mCluster << ", user:" << mUser <<
        ", pool:" << mPool << ".");
    mInited = true;
    return BIO_OK;
}

void DlCephSystem::Stop()
{
    if (mIoCtx != nullptr) {
        mRadosIoCtxDestroy(mIoCtx);
    }
    if (mConn != nullptr) {
        mRadosShutdown(mConn);
    }
    mIoCtx = nullptr;
    mConn = nullptr;
    mInited = false;
}

BResult DlCephSystem::Put(const char *key, const char *value, const size_t len)
{
    int ret = BIO_UFS_IOERR;
    ChkTrue(mIoCtx != nullptr, BIO_NOT_READY, "Io context is nullptr, because of underFS not ready.");
    LOG_DEBUG("UnderFs put key:" << key);

    BIO_TRACE_START(UFS_TRACE_PUT);
    BIO_TP_START(SERVER_UNDERFS_PUT, &ret, -1);
    ret = mRadosWrite(mIoCtx, key, value, len, 0);
    BIO_TP_END;
    BIO_TRACE_END(UFS_TRACE_PUT, ret);
    if (ret < 0) {
        LOG_ERROR("Failed to write object, ret:" << ret << ".");
        return BIO_UFS_IOERR;
    }
    return BIO_OK;
}

BResult DlCephSystem::Get(const char *key, char *value, const size_t len, const uint64_t off)
{
    int ret = BIO_UFS_IOERR;
    ChkTrue(mIoCtx != nullptr, BIO_NOT_READY, "Io context is nullptr, because of underFS not ready.");
    LOG_DEBUG("UnderFs get key:" << key);

    BIO_TRACE_START(UFS_TRACE_GET);
    BIO_TP_START(SERVER_UNDERFS_GET, &ret, -1);
    ret = mRadosRead(mIoCtx, key, value, len, off);
    BIO_TP_END;
    BIO_TP_START(UNDERFS_CEPH_GET_FAIL, &ret, (-ENOENT));
    BIO_TP_END;
    int res = (ret < 0) ? BIO_UFS_IOERR : BIO_OK;
    BIO_TRACE_END(UFS_TRACE_GET, res);
    if (ret == -ENOENT) {
        LOG_WARN("Fail to get object " << key << ", not exist.");
        return BIO_NOT_EXISTS;
    }
    if (ret < 0) {
        LOG_ERROR("Failed to read object " << key << ", ret:" << ret);
        return BIO_UFS_IOERR;
    }
    return BIO_OK;
}

BResult DlCephSystem::Delete(const char *key)
{
    int ret = BIO_UFS_IOERR;
    ChkTrue(mIoCtx != nullptr, BIO_NOT_READY, "Io context is nullptr, because of underFS not ready.");
    LOG_DEBUG("UnderFs delete key:" << key);

    BIO_TRACE_START(UFS_TRACE_DEL);
    BIO_TP_START(SERVER_UNDERFS_DELETE, &ret, -1);
    ret = mRadosRemove(mIoCtx, key);
    BIO_TP_END;
    BIO_TP_START(UNDERFS_CEPH_DELETE_NOT_EXIST, &ret, (-ENOENT));
    BIO_TP_END;
    BIO_TRACE_END(UFS_TRACE_DEL, ret);
    if (ret == -ENOENT) {
        LOG_WARN("Fail to check file, not exist, " << key);
        return BIO_NOT_EXISTS;
    }
    if (ret < 0) {
        LOG_ERROR("Failed to remove object, ret:" << ret);
        return BIO_UFS_IOERR;
    }
    return BIO_OK;
}

BResult DlCephSystem::Stat(const char *key, ObjStat &stat)
{
    int ret = BIO_UFS_IOERR;
    ChkTrue(mIoCtx != nullptr, BIO_NOT_READY, "Io context is nullptr, because of underFS not ready.");
    LOG_DEBUG("UnderFs stat key:" << key);

    BIO_TRACE_START(UFS_TRACE_STAT);
    BIO_TP_START(SERVER_UNDERFS_STAT, &ret, -1);
    ret = mRadosStat(mIoCtx, key, &stat.size, &stat.time);
    BIO_TP_END;
    BIO_TP_START(UNDERFS_CEPH_STAT_NOT_EXIST, &ret, (-ENOENT));
    BIO_TP_END;
    BIO_TRACE_END(UFS_TRACE_STAT, ret);
    if (ret == -ENOENT) {
        LOG_WARN("Fail to stat object " << key << ", not exist.");
        return BIO_NOT_EXISTS;
    }
    if (ret < 0) {
        LOG_ERROR("Failed to stat object " << key << ", ret:" << ret);
        return BIO_UFS_IOERR;
    }
    BIO_TP_START(SERVER_UNDERFS_STAT_SIZE, &stat.size, IO_MAX_LEN + 1);
    BIO_TP_END;
    if (stat.size > IO_MAX_LEN) {
        LOG_ERROR("invalid file size: " << stat.size << ".");
        return BIO_NOT_EXISTS;
    }
    return BIO_OK;
}

BResult DlCephSystem::List(const char *prefix, std::unordered_map<std::string, DlCephSystem::ObjStat> &objStat)
{
    int ret = BIO_UFS_IOERR;
    ChkTrue(mIoCtx != nullptr, BIO_NOT_READY, "Io context is nullptr, because of underFS not ready.");
    LOG_DEBUG("UnderFs list prefix:" << prefix);

    rados_list_ctx_t listCtx = nullptr;
    BIO_TP_START(SERVER_UNDERFS_LIST, &ret, -1);
    ret = mRadosNobjectsListOpen(mIoCtx, &listCtx);
    BIO_TP_END;
    if (ret < 0) {
        LOG_ERROR("Failed to list open, ret:" << ret);
        return BIO_UFS_IOERR;
    }

    BIO_TRACE_START(UFS_TRACE_LIST);
    char *entry = nullptr;
    size_t prefixLength = strlen(prefix);
    while (mRadosNobjectsListNext(listCtx, const_cast<const char **>(&entry), nullptr, nullptr) == 0) {
        if (memcmp(entry, prefix, prefixLength) == 0) {
            ObjStat objectStat;
            ret = this->Stat(entry, objectStat);
            if (ret != 0) {
                LOG_ERROR("Fail to stat object " << entry << ", ret: " << ret << ".");
                continue;
            }
            objStat.insert({ entry, objectStat });
        }
    }
    mRadosNobjectsListClose(listCtx);
    BIO_TRACE_END(UFS_TRACE_LIST, ret);
    return BIO_OK;
}

void DlCephSystem::LoadCephConfig()
{
    auto instance = UnderFsConfig::Instance();
    if (instance == nullptr) {
        return;
    }
    BioConfig::UnderFsConfig config = instance->GetUnderFsConfig();
    mCfgPath = config.cephConfig.cfgPath;
    mCluster = config.cephConfig.cluster;
    mUser = config.cephConfig.user;
    mPool = config.cephConfig.pools.at(0);
}

}
}
